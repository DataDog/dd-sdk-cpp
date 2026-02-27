// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "crash_handler_buildid_cache.hpp"

#ifdef _WIN32
// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>  // ToolHelp32 snapshot APIs; must follow windows.h
// clang-format on

#include <cstdio>
#endif

#ifdef __linux__
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#endif

namespace datadog::platform {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
ModuleBuildIdCache g_module_build_id_cache = {};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

const char* LookupCachedBuildId(uintptr_t base_address) {
  // Linear search through cache (async-signal-safe)
  const size_t count = g_module_build_id_cache.num_entries;
  for (size_t i = 0; i < count; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    const CachedModuleBuildId& entry = g_module_build_id_cache.entries[i];
    if (entry.valid && entry.base_address == base_address) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
      return entry.build_id;
    }
  }
  return nullptr;
}

#ifdef _WIN32

// Maps an RVA (relative virtual address) to a file offset by walking the section
// header array. Returns true and writes the file offset to `out_offset` if the RVA
// falls within one of the sections; returns false otherwise.
static bool rva_to_file_offset(
    const IMAGE_SECTION_HEADER* sections,
    WORD num_sections,
    DWORD rva,
    DWORD* out_offset
) {
  for (WORD i = 0; i < num_sections; ++i) {
    const DWORD vaddr = sections[i].VirtualAddress;
    // Use VirtualSize when available; fall back to SizeOfRawData
    DWORD vsize = sections[i].Misc.VirtualSize;
    if (vsize == 0) vsize = sections[i].SizeOfRawData;
    if (rva >= vaddr && rva < vaddr + vsize) {
      *out_offset = sections[i].PointerToRawData + (rva - vaddr);
      return true;
    }
  }
  return false;
}

/**
 * Extract PE GUID+Age from Windows binary file.
 *
 * Opens the PE file at `module_path`, reads the debug directory to find the CodeView
 * record, extracts the GUID and Age, and formats them as uppercase hex with no
 * delimiters (e.g., "F4A7B2C3D1E5F6789ABCDEF0123456785").
 *
 * Returns true on success (build ID written to `out_buffer`), false on failure.
 * This function uses file I/O and is NOT async-signal-safe.
 */
static bool ExtractPEBuildIdSafe(
    const char* module_path, char* out_buffer, size_t buffer_size
) {
  if (buffer_size < 42) {  // 32 hex chars for GUID + up to 10 for Age + null
    return false;
  }

  HANDLE file = CreateFileA(
      module_path,
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
  );
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  // Read DOS header to get offset to NT headers
  IMAGE_DOS_HEADER dos_header;
  DWORD bytes_read = 0;
  if (!ReadFile(file, &dos_header, sizeof(dos_header), &bytes_read, nullptr) ||
      bytes_read != sizeof(dos_header) || dos_header.e_magic != IMAGE_DOS_SIGNATURE) {
    CloseHandle(file);
    return false;
  }

  // Seek to NT headers
  if (SetFilePointer(file, dos_header.e_lfanew, nullptr, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER) {
    CloseHandle(file);
    return false;
  }

  // Read NT headers (64-bit)
  IMAGE_NT_HEADERS64 nt_headers;
  if (!ReadFile(file, &nt_headers, sizeof(nt_headers), &bytes_read, nullptr) ||
      bytes_read != sizeof(nt_headers) || nt_headers.Signature != IMAGE_NT_SIGNATURE) {
    CloseHandle(file);
    return false;
  }

  // Locate debug directory in data directories
  if (nt_headers.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) {
    CloseHandle(file);
    return false;
  }

  const IMAGE_DATA_DIRECTORY& debug_dir =
      nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  if (debug_dir.Size == 0) {
    CloseHandle(file);
    return false;
  }

  // Read PE section headers to convert the debug directory RVA to a file offset.
  // IMAGE_DATA_DIRECTORY.VirtualAddress is an RVA, not a file offset; the section
  // headers are needed to perform the mapping.
  const WORD num_sections = nt_headers.FileHeader.NumberOfSections;
  if (num_sections == 0 || num_sections > 96) {
    CloseHandle(file);
    return false;
  }

  // Section headers immediately follow the optional header, which starts at
  // e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader).
  const LONG sections_offset =
      dos_header.e_lfanew +
      static_cast<LONG>(FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader)) +
      nt_headers.FileHeader.SizeOfOptionalHeader;
  if (SetFilePointer(file, sections_offset, nullptr, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER) {
    CloseHandle(file);
    return false;
  }

  IMAGE_SECTION_HEADER sections[96];
  const DWORD sections_bytes = num_sections * sizeof(IMAGE_SECTION_HEADER);
  if (!ReadFile(file, sections, sections_bytes, &bytes_read, nullptr) ||
      bytes_read != sections_bytes) {
    CloseHandle(file);
    return false;
  }

  DWORD debug_dir_offset = 0;
  if (!rva_to_file_offset(
          sections, num_sections, debug_dir.VirtualAddress, &debug_dir_offset
      )) {
    CloseHandle(file);
    return false;
  }

  // Read debug directory entries
  const DWORD num_entries = debug_dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
  for (DWORD i = 0; i < num_entries; ++i) {
    if (SetFilePointer(
            file,
            debug_dir_offset + i * sizeof(IMAGE_DEBUG_DIRECTORY),
            nullptr,
            FILE_BEGIN
        ) == INVALID_SET_FILE_POINTER) {
      continue;
    }

    IMAGE_DEBUG_DIRECTORY debug_entry;
    if (!ReadFile(file, &debug_entry, sizeof(debug_entry), &bytes_read, nullptr) ||
        bytes_read != sizeof(debug_entry)) {
      continue;
    }

    // Look for CodeView debug information (type 2)
    if (debug_entry.Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
      // Read CodeView record
      if (SetFilePointer(file, debug_entry.PointerToRawData, nullptr, FILE_BEGIN) ==
          INVALID_SET_FILE_POINTER) {
        continue;
      }

      // CodeView record starts with signature (4 bytes), then GUID (16 bytes), then Age
      struct CodeViewRecord {
        DWORD signature;
        GUID guid;
        DWORD age;
      };

      CodeViewRecord cv_record;
      if (!ReadFile(file, &cv_record, sizeof(cv_record), &bytes_read, nullptr) ||
          bytes_read != sizeof(cv_record)) {
        continue;
      }

      // Check for RSDS signature (0x53445352)
      if (cv_record.signature != 0x53445352) {
        continue;
      }

      // Format GUID and Age as uppercase hex: Data1-Data2-Data3-Data4[0-7]-Age
      snprintf(
          out_buffer,
          buffer_size,
          "%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%u",
          cv_record.guid.Data1,
          cv_record.guid.Data2,
          cv_record.guid.Data3,
          cv_record.guid.Data4[0],
          cv_record.guid.Data4[1],
          cv_record.guid.Data4[2],
          cv_record.guid.Data4[3],
          cv_record.guid.Data4[4],
          cv_record.guid.Data4[5],
          cv_record.guid.Data4[6],
          cv_record.guid.Data4[7],
          cv_record.age
      );

      CloseHandle(file);
      return true;
    }
  }

  CloseHandle(file);
  return false;
}

void InitializeModuleBuildIdCache() {
  g_module_build_id_cache.num_entries = 0;

  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId()
  );
  if (snapshot == INVALID_HANDLE_VALUE) {
    return;
  }

  MODULEENTRY32 entry;
  entry.dwSize = sizeof(entry);

  if (Module32First(snapshot, &entry)) {
    do {
      if (g_module_build_id_cache.num_entries >= kMaxCachedModules) {
        break;
      }

      char build_id[kMaxBuildIdLength];
      if (ExtractPEBuildIdSafe(entry.szExePath, build_id, sizeof(build_id))) {
        const size_t entry_idx = g_module_build_id_cache.num_entries;
        auto& cached = g_module_build_id_cache.entries[entry_idx];
        cached.base_address = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        snprintf(cached.build_id, kMaxBuildIdLength, "%s", build_id);
        cached.valid = true;
        // Make entry visible to readers only after fully written
        g_module_build_id_cache.num_entries = entry_idx + 1;
      }
    } while (Module32Next(snapshot, &entry));
  }

  CloseHandle(snapshot);
}

#endif  // _WIN32

#ifdef __linux__

/**
 * Extract ELF build ID from Linux binary file.
 *
 * Opens the ELF file at `module_path`, reads program headers to find PT_NOTE
 * segments, parses note entries to find the NT_GNU_BUILD_ID note, and formats the
 * build ID as lowercase hex (e.g., "8c9d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d").
 *
 * Returns true on success (build ID written to `out_buffer`), false on failure.
 * This function uses file I/O and is NOT async-signal-safe.
 */
static bool ExtractElfBuildIdSafe(
    const char* module_path, char* out_buffer, size_t buffer_size
) {
  if (buffer_size < 41) {  // Typical 20-byte build ID = 40 hex chars + null
    return false;
  }

  int fd = open(module_path, O_RDONLY);
  if (fd < 0) {
    return false;
  }

  // Read ELF header
  Elf64_Ehdr ehdr;
  if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
    close(fd);
    return false;
  }

  // Validate ELF magic
  if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
      ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
    close(fd);
    return false;
  }

  // Check if 64-bit or 32-bit
  const bool is_64bit = (ehdr.e_ident[EI_CLASS] == ELFCLASS64);

  if (!is_64bit) {
    // Handle 32-bit ELF (not shown for brevity - would need Elf32_* types)
    close(fd);
    return false;
  }

  // Read program headers
  for (int i = 0; i < ehdr.e_phnum; ++i) {
    // Seek to phdr table offset before each read to avoid lseek corruption
    const off_t phdr_offset = ehdr.e_phoff + i * sizeof(Elf64_Phdr);
    if (lseek(fd, phdr_offset, SEEK_SET) != phdr_offset) {
      continue;
    }

    Elf64_Phdr phdr;
    if (read(fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
      continue;
    }

    // Look for PT_NOTE segments
    if (phdr.p_type == PT_NOTE) {
      // Seek to note segment
      if (lseek(fd, phdr.p_offset, SEEK_SET) != static_cast<off_t>(phdr.p_offset)) {
        continue;
      }

      // Read note segment data
      char note_data[4096];
      const size_t note_size =
          (phdr.p_filesz < sizeof(note_data)) ? phdr.p_filesz : sizeof(note_data);
      if (read(fd, note_data, note_size) != static_cast<ssize_t>(note_size)) {
        continue;
      }

      // Parse note entries
      size_t offset = 0;
      while (offset + sizeof(Elf64_Nhdr) <= note_size) {
        const auto* nhdr = reinterpret_cast<const Elf64_Nhdr*>(note_data + offset);
        offset += sizeof(Elf64_Nhdr);

        // Align name and desc to 4-byte boundaries
        const size_t name_size_aligned = (nhdr->n_namesz + 3) & ~3;
        const size_t desc_size_aligned = (nhdr->n_descsz + 3) & ~3;

        if (offset + name_size_aligned + desc_size_aligned > note_size) {
          break;
        }

        const char* name = note_data + offset;
        offset += name_size_aligned;

        const uint8_t* desc = reinterpret_cast<const uint8_t*>(note_data + offset);
        offset += desc_size_aligned;

        // Look for GNU build ID (type 3, name "GNU")
        if (nhdr->n_type == NT_GNU_BUILD_ID && nhdr->n_namesz == 4 &&
            memcmp(name, "GNU", 4) == 0) {
          // Format build ID as lowercase hex
          size_t out_idx = 0;
          // Ensure room for 2 hex chars + null terminator
          for (size_t byte_idx = 0;
               byte_idx < nhdr->n_descsz && out_idx <= buffer_size - 3;
               ++byte_idx) {
            snprintf(
                out_buffer + out_idx, buffer_size - out_idx, "%02x", desc[byte_idx]
            );
            out_idx += 2;
          }
          out_buffer[out_idx] = '\0';

          close(fd);
          return true;
        }
      }
    }
  }

  close(fd);
  return false;
}

void InitializeModuleBuildIdCache() {
  g_module_build_id_cache.num_entries = 0;

  FILE* maps = fopen("/proc/self/maps", "r");
  if (!maps) {
    return;
  }

  char line[4096];
  // Track which modules we've already cached (by pathname)
  // Static storage to avoid 64KB stack allocation
  static char cached_paths[kMaxCachedModules][256];
  size_t num_cached_paths = 0;

  while (fgets(line, sizeof(line), maps) &&
         g_module_build_id_cache.num_entries < kMaxCachedModules) {
    uintptr_t start_addr = 0;
    uintptr_t end_addr = 0;
    char perms[5] = {};
    char pathname[256] = {};

    // Parse: address_start-address_end perms offset dev:inode pathname
    // NOLINTNEXTLINE(cert-err34-c)
    if (sscanf(
            line,
            "%lx-%lx %4s %*x %*x:%*x %*d %255s",
            &start_addr,
            &end_addr,
            perms,
            pathname
        ) == 4) {
      // Only executable segments with absolute paths
      if (strchr(perms, 'x') && pathname[0] == '/') {
        // Check if already cached (avoid duplicate entries for same binary)
        bool already_cached = false;
        for (size_t i = 0; i < num_cached_paths; ++i) {
          if (strcmp(cached_paths[i], pathname) == 0) {
            already_cached = true;
            break;
          }
        }

        if (!already_cached && num_cached_paths < kMaxCachedModules) {
          char build_id[kMaxBuildIdLength];
          if (ExtractElfBuildIdSafe(pathname, build_id, sizeof(build_id))) {
            const size_t entry_idx = g_module_build_id_cache.num_entries;
            auto& cached = g_module_build_id_cache.entries[entry_idx];
            cached.base_address = start_addr;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            snprintf(cached.build_id, kMaxBuildIdLength, "%s", build_id);
            cached.valid = true;
            // Make entry visible to readers only after fully written
            g_module_build_id_cache.num_entries = entry_idx + 1;

            // Track this path to avoid duplicates
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            snprintf(cached_paths[num_cached_paths], 256, "%s", pathname);
            ++num_cached_paths;
          }
        }
      }
    }
  }

  fclose(maps);
}

#endif  // __linux__

#ifdef __APPLE__

void InitializeModuleBuildIdCache() {
  // Not needed on macOS—build IDs extracted from memory at crash time
}

#endif  // __APPLE__

}  // namespace datadog::platform
