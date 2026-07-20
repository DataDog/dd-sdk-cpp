// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/handlers/inprocess/buildid_cache.hpp"

#ifdef _WIN32
// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>  // ToolHelp32 snapshot APIs; must follow windows.h
// clang-format on

#include <cstdio>
#endif

#ifdef __linux__
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#endif

namespace datadog::impl {

// This file contains functions that extract build IDs from binary files (PE on Windows,
// ELF on Linux). The Linux implementation must be usable from async-signal-safe
// contexts, so it uses low-level C-style I/O.
// NOLINTBEGIN(bugprone-unchecked-string-to-number-conversion)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

// Explicitly zero-initialize module build cache, for clarity
ModuleBuildIdCache g_module_build_id_cache = {};

#ifdef _WIN32

/**
 * Maps a relative virtual address (RVA) to a file offset by walking the section header
 * array. Returns true and writes the file offset to `out_offset` if the RVA falls
 * within one of the sections; returns false otherwise.
 */
static bool rva_to_file_offset(
    const IMAGE_SECTION_HEADER* sections,
    WORD num_sections,
    DWORD rva,
    DWORD* out_offset
) {
  // Iterate through the headers describing each section of the PE binary, until we find
  // the section that contains the relative virtual address we want to resolve
  for (WORD i = 0; i < num_sections; ++i) {
    // VirtualSize is the size of the section in memory; fall back to SizeOfRawData
    // (size of section in the file) if not available
    DWORD section_size = sections[i].Misc.VirtualSize;
    if (section_size == 0) {
      section_size = sections[i].SizeOfRawData;
    }

    // VirtualAddress denotes where the section is loaded into memory
    const DWORD section_start = sections[i].VirtualAddress;
    const DWORD section_end = section_start + section_size;

    // If the RVA we're looking for lies within this section, then we can compute the
    // offset into the file that corresponds to the start of the section
    if (rva >= section_start && rva < section_end) {
      *out_offset = sections[i].PointerToRawData + (rva - section_start);
      return true;
    }
  }
  return false;
}

/**
 * Extracts PE build ID from optional header (template helper for 32-bit or 64-bit).
 *
 * Locates the debug directory using the DataDirectory table in `opt_header`, converts
 * the debug directory RVA to a file offset using `sections`, seeks to that offset in
 * `file`, and iterates through IMAGE_DEBUG_DIRECTORY entries to find the CodeView
 * record containing the PDB GUID and Age. Formats the build ID as uppercase hex
 * (GUID+Age) and writes it to `out_buffer`.
 *
 * This template helper is instantiated with `OhdrType` = IMAGE_OPTIONAL_HEADER32 or
 * IMAGE_OPTIONAL_HEADER64 by `extract_pe_build_id` after architecture detection, to
 * handle both 32-bit and 64-bit PE files with the same logic.
 *
 * Returns true on success (build ID written to `out_buffer`), false if the file lacks
 * debug information or cannot be read.
 */
template <typename OhdrType>
static bool extract_pe_build_id_from_optional_header(
    HANDLE file,
    const OhdrType& opt_header,
    const IMAGE_SECTION_HEADER* sections,
    WORD num_sections,
    char* out_buffer,
    size_t buffer_size
) {
  // IMAGE_OPTIONAL_HEADER64/32 contains a lookup table, DataDirectory, with predefined
  // indices for common data that's encoded in the PE file. The DEBUG entry tells us
  // where we can find an array of IMAGE_DEBUG_DIRECTORY values.
  if (opt_header.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) {
    // File does not contain enough extra sections; unable to read debug info
    return false;
  }
  const IMAGE_DATA_DIRECTORY& debug_dir =
      opt_header.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];

  // Our IMAGE_DATA_DIRECTORY value gives us the virtual address where the
  // IMAGE_DEBUG_DIRECTORY array will be loaded, relative to the base address of the
  // module. We need to convert this relative virtual address (RVA) to a file offset, by
  // finding the PE section that includes the IMAGE_DEBUG_DIRECTORY array and computing
  // an offset from that section's position in the file.
  const DWORD debug_dir_rva = debug_dir.VirtualAddress;
  DWORD debug_dir_offset = 0;
  if (!rva_to_file_offset(sections, num_sections, debug_dir_rva, &debug_dir_offset)) {
    // Failed to compute file offset; unable to read debug info
    return false;
  }

  // Seek to the file offset where the IMAGE_DEBUG_DIRECTORY array begins, so we can
  // read each entry sequentially until we find the data we need
  if (SetFilePointer(file, debug_dir_offset, nullptr, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER) {
    // Seek failed; unable to read debug info
    return false;
  }

  // A PE binary may contain multiple debug directory entries, each with a different
  // type. We're looking for an entry with type IMAGE_DEBUG_TYPE_CODEVIEW, indicating a
  // CodeView record that contains the PDB GUID and Age values.
  DWORD bytes_read = 0;
  const DWORD num_debug_entries = debug_dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
  for (DWORD i = 0; i < num_debug_entries; ++i) {
    // Read the next debug entry from the file
    IMAGE_DEBUG_DIRECTORY debug_entry;
    if (!ReadFile(file, &debug_entry, sizeof(debug_entry), &bytes_read, nullptr) ||
        bytes_read != sizeof(debug_entry)) {
      // Read failed; unable to resolve CodeView record
      return false;
    }

    // If this entry does not describe a CodeView record, ignore it and continue reading
    // the next entry
    if (debug_entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW) {
      continue;
    }

    // debug_entry describes where we can find the CodeView record that contains our PDB
    // GUID and Age: PointerToRawData gives us an exact offset into the file, so seek to
    // that position
    if (SetFilePointer(file, debug_entry.PointerToRawData, nullptr, FILE_BEGIN) ==
        INVALID_SET_FILE_POINTER) {
      // Seek failed; unable to read CodeView record
      return false;
    }

    // We can now read the required data encoded in CodeView format
    struct CodeViewRecord {
      DWORD signature;  // "RSDS" = 0x53445352
      GUID guid;        // Unique identifier for this build
      DWORD age;        // Incremental counter
      // Followed by null-terminated PDB path string
    };
    CodeViewRecord cv_record;
    if (!ReadFile(file, &cv_record, sizeof(cv_record), &bytes_read, nullptr) ||
        bytes_read != sizeof(cv_record)) {
      // Read failed; unable to parse CodeView record
      return false;
    }
    if (cv_record.signature != 0x53445352) {
      // Invalid CodeView format; abort
      return false;
    }

    // Write our build ID to the output buffer, formatting GUID and Age as uppercase
    // hex without delimiters: {Data1}{Data2}{Data3}{Data4[0-7]}{Age}
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
    return true;
  }

  // We've iterated over all debug entries without finding a CodeView record: there is
  // no build ID encoded in this file
  return false;
}

/**
 * Extracts PE GUID+Age from Windows binary file.
 *
 * Opens the PE file at `module_path`, reads the debug directory to find the CodeView
 * record, extracts the GUID and Age, and formats them as uppercase hex with no
 * delimiters (e.g., "F4A7B2C3D1E5F6789ABCDEF0123456785").
 *
 * Supports both 32-bit (IMAGE_OPTIONAL_HEADER32) and 64-bit (IMAGE_OPTIONAL_HEADER64)
 * PE files by detecting the architecture from IMAGE_FILE_HEADER.Machine.
 *
 * Returns true on success (build ID written to `out_buffer`), false on failure. This
 * function uses file I/O and is NOT async-signal-safe.
 */
static bool extract_pe_build_id(
    const char* module_path, char* out_buffer, size_t buffer_size
) {
  // Require a large enough output buffer to fit any PE build ID: 32 hex chars for GUID,
  // up to 10 decimal digits for Age, plus null terminator
  if (buffer_size < 42) {
    // Insufficient buffer size; abort
    return false;
  }

  // TODO: Accept const wchar_t*, use CreateFileW, use MODULEENTRY32W
  // Open the PE file for read
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

  // A PE binary begins with a DOS header, which contains an e_lfanew value denoting
  // the offset into the file at which we can find the NT header:
  //
  // - 0x0000 [DOS header (IMAGE_DOS_HEADER) - 64 bytes]
  // - 0x0040 [DOS stub - variable size]
  //
  // The NT header consists of a 4-byte magic constant "PE\0\0", followed by an
  // IMAGE_FILE_HEADER, then an IMAGE_OPTIONAL_HEADER64 or IMAGE_OPTIONAL_HEADER32
  // value
  // ("optional" in the context of object files; actually required for compiled PE
  // binaries), followed by a packed array of IMAGE_SECTION_HEADER values describing
  // each of the sections (e.g. .text, .data, .bss) in the PE file.
  //
  // Assuming dos_header.e_lfanew is 0x0080, then we might find:
  //
  // - 0x0080: ["PE\0\0" - 4 bytes]
  // - 0x0084: [IMAGE_FILE_HEADER - 20 bytes]
  // - 0x0098: [IMAGE_OPTIONAL_HEADER64|32 - file_header.SizeOfOptionalHeader]
  // - 0x????: [Section headers (IMAGE_SECTION_HEADER) * file_header.NumberOfSections]

  // From the start of the file, read the DOS header so we can get the offset to the
  // NT header
  IMAGE_DOS_HEADER dos_header;
  DWORD bytes_read = 0;
  if (!ReadFile(file, &dos_header, sizeof(dos_header), &bytes_read, nullptr) ||
      bytes_read != sizeof(dos_header) || dos_header.e_magic != IMAGE_DOS_SIGNATURE) {
    // Read failed or invalid DOS header; abort
    CloseHandle(file);
    return false;
  }

  // Seek to the position indicated by e_lfanew, so we can start reading from the NT
  // header
  if (SetFilePointer(file, dos_header.e_lfanew, nullptr, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER) {
    // Seek failed; abort
    CloseHandle(file);
    return false;
  }

  // Read the next 4 bytes to verify the NT signature ("PE\0\0", i.e. 0x00004550)
  DWORD nt_signature = 0;
  if (!ReadFile(file, &nt_signature, sizeof(nt_signature), &bytes_read, nullptr) ||
      bytes_read != sizeof(nt_signature) || nt_signature != IMAGE_NT_SIGNATURE) {
    // Read failed or invalid NT signature; abort
    CloseHandle(file);
    return false;
  }

  // Read the NT file header value so we can determine the architecture and number of
  // sections in the file
  IMAGE_FILE_HEADER file_header;
  if (!ReadFile(file, &file_header, sizeof(file_header), &bytes_read, nullptr) ||
      bytes_read != sizeof(file_header)) {
    // Read failed; abort
    CloseHandle(file);
    return false;
  }

  // Determine architecture and read the appropriate IMAGE_OPTIONAL_HEADER value.
  // file_header.Machine indicates the architecture for which the PE binary is
  // compiled; file_header.SizeOfOptionalHeader denotes the size of the
  // IMAGE_OPTIONAL_HEADER that immediately follows the IMAGE_FILE_HEADER
  union {
    IMAGE_OPTIONAL_HEADER32 i386;
    IMAGE_OPTIONAL_HEADER64 amd64;
  } opt_header;
  bool is_64bit = false;
  if (file_header.Machine == IMAGE_FILE_MACHINE_AMD64) {
    is_64bit = true;
    if (file_header.SizeOfOptionalHeader != sizeof(opt_header.amd64) ||
        !ReadFile(
            file, &opt_header.amd64, sizeof(opt_header.amd64), &bytes_read, nullptr
        ) ||
        bytes_read != sizeof(opt_header.amd64)) {
      // Read failed or unexpected optional header size for 64-bit arch; abort
      CloseHandle(file);
      return false;
    }
  } else if (file_header.Machine == IMAGE_FILE_MACHINE_I386) {
    is_64bit = false;
    if (file_header.SizeOfOptionalHeader != sizeof(opt_header.i386) ||
        !ReadFile(
            file, &opt_header.i386, sizeof(opt_header.i386), &bytes_read, nullptr
        ) ||
        bytes_read != sizeof(opt_header.i386)) {
      // Read failed or unexpected optional header size for 32-bit arch; abort
      CloseHandle(file);
      return false;
    }
  } else {
    // Unsupported architecture; abort
    CloseHandle(file);
    return false;
  }

  // Our file handle is now positioned at the start of the IMAGE_SECTION_HEADER array:
  // read these section headers into memory so we can efficiently traverse them while
  // computing offsets etc.
  static const size_t max_sections = 96;
  IMAGE_SECTION_HEADER sections[max_sections];

  // Typical PE files have 5-10 sections, while a complex binary may have 20-30: if
  // the file reports more sections than our reasonable upper limit, ignore it
  const WORD num_sections = file_header.NumberOfSections;
  if (num_sections == 0 || num_sections > max_sections) {
    // Invalid section count; abort
    CloseHandle(file);
    return false;
  }

  // Read from the file to populate our sections array
  const DWORD num_section_header_bytes = num_sections * sizeof(IMAGE_SECTION_HEADER);
  if (!ReadFile(file, sections, num_section_header_bytes, &bytes_read, nullptr) ||
      bytes_read != num_section_header_bytes) {
    // Read failed; abort
    CloseHandle(file);
    return false;
  }

  // Now that we've loaded the optional header and the set of PE sections (required for
  // address resolution), we can read from the optional header to locate the
  // IMAGE_DEBUG_DIRECTORY records within the file, then find the CodeView record that
  // includes PDB GUID and Age, then populate out_buffer with the canonical Build ID
  // representation of that data
  bool success = false;
  if (is_64bit) {
    // Call helper func with OhdrType = IMAGE_OPTIONAL_HEADER64
    success = extract_pe_build_id_from_optional_header(
        file, opt_header.amd64, sections, num_sections, out_buffer, buffer_size
    );
  } else {
    // Call helper func with OhdrType = IMAGE_OPTIONAL_HEADER32
    success = extract_pe_build_id_from_optional_header(
        file, opt_header.i386, sections, num_sections, out_buffer, buffer_size
    );
  }

  CloseHandle(file);
  return success;
}

void PopulateBuildIdCache() {
  // Reset the cache to empty state before repopulating
  g_module_build_id_cache.num_entries = 0;

  // Use the ToolHelp32 API to enumerate all modules (DLLs and EXEs) loaded in the
  // current process. TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32 ensures we capture both
  // 64-bit and 32-bit modules in a WOW64 process.
  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId()
  );
  if (snapshot == INVALID_HANDLE_VALUE) {
    // Snapshot creation failed; cache remains empty
    return;
  }

  // MODULEENTRY32 is populated by Module32First/Module32Next with information about
  // each loaded module. The dwSize field must be initialized before the first call.
  MODULEENTRY32 entry;
  entry.dwSize = sizeof(entry);

  // Iterate through all loaded modules, starting with Module32First to get the first
  // module, then repeatedly calling Module32Next until no more modules remain
  if (Module32First(snapshot, &entry)) {
    do {
      // Stop if we've reached our cache capacity
      if (g_module_build_id_cache.num_entries >= kMaxCachedModules) {
        break;
      }

      // Attempt to extract the build ID from this module's PE file (szExePath contains
      // the full path to the module's file on disk)
      char build_id[kMaxBuildIdLength];
      if (extract_pe_build_id(entry.szExePath, build_id, sizeof(build_id))) {
        // Successfully extracted build ID: add this module to the cache, recording its
        // base address (where it's loaded in memory) and build ID string
        const size_t entry_idx = g_module_build_id_cache.num_entries;
        auto& cached = g_module_build_id_cache.entries[entry_idx];
        cached.base_address = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
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
 * Extracts ELF build ID from program headers (template helper for 32-bit or 64-bit).
 *
 * Reads the program header array from `ehdr` to locate PT_NOTE segments, which
 * contain note entries with auxiliary information about the binary. Parses each note
 * entry to find an NT_GNU_BUILD_ID note (type 3, name "GNU"), extracts the build ID
 * bytes from the note descriptor, and formats them as lowercase hex (e.g.,
 * "8c9d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d") in `out_buffer`.
 *
 * This template helper is instantiated with `EhdrType` = Elf32_Ehdr or Elf64_Ehdr,
 * `PhdrType` = Elf32_Phdr or Elf64_Phdr, and `NhdrType` = Elf32_Nhdr or Elf64_Nhdr by
 * `extract_elf_build_id` after architecture detection, to handle both 32-bit and
 * 64-bit ELF files with the same logic.
 *
 * Returns true on success (build ID written to `out_buffer`), false if the file lacks
 * a GNU build ID note or cannot be read.
 */
template <typename EhdrType, typename PhdrType, typename NhdrType>
static bool extract_elf_build_id_from_headers(
    int fd, const EhdrType& ehdr, char* out_buffer, size_t buffer_size
) {
  // Iterate through the program headers to find PT_NOTE segments, which may contain
  // note entries including the GNU build ID
  for (int i = 0; i < ehdr.e_phnum; ++i) {
    // Seek to the position of this program header in the file before reading it. We
    // perform an explicit seek before each read (rather than relying on sequential file
    // position) to avoid lseek corruption issues in signal handlers where multiple
    // threads may share the same file descriptor offset.
    const off_t phdr_offset = ehdr.e_phoff + (i * sizeof(PhdrType));
    if (lseek(fd, phdr_offset, SEEK_SET) != phdr_offset) {
      // Seek failed; skip to next header
      continue;
    }

    PhdrType phdr{};
    if (read(fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
      // Read failed; skip to next header
      continue;
    }

    // PT_NOTE segments contain note entries, which are auxiliary information records.
    // One of these note entries may be the GNU build ID we're looking for.
    if (phdr.p_type == PT_NOTE) {
      // Seek to the file offset where this PT_NOTE segment begins
      if (lseek(fd, phdr.p_offset, SEEK_SET) != static_cast<off_t>(phdr.p_offset)) {
        // Seek failed; skip to next segment
        continue;
      }

      // Read the note segment data into a fixed-size buffer. We cap the read at 4096
      // bytes to avoid excessive stack allocation; typical PT_NOTE segments containing
      // build IDs are much smaller.
      char note_data[4096];
      const size_t note_segment_size =
          (phdr.p_filesz < sizeof(note_data)) ? phdr.p_filesz : sizeof(note_data);
      if (read(fd, note_data, note_segment_size) !=
          static_cast<ssize_t>(note_segment_size)) {
        // Read failed; skip to next segment
        continue;
      }

      // A PT_NOTE segment contains one or more note entries, each with the structure:
      //
      // - Nhdr (12 bytes on 32-bit, 12 bytes on 64-bit): header with n_namesz,
      //   n_descsz, n_type
      // - name (n_namesz bytes, padded to 4-byte alignment): null-terminated string
      // - desc (n_descsz bytes, padded to 4-byte alignment): descriptor data
      //
      // We're looking for a note entry with n_type = NT_GNU_BUILD_ID (3), name =
      // "GNU\0", and descriptor containing the raw build ID bytes.
      size_t offset = 0;
      while (offset + sizeof(NhdrType) <= note_segment_size) {
        const auto* nhdr = reinterpret_cast<const NhdrType*>(note_data + offset);
        offset += sizeof(NhdrType);

        // Note names and descriptors must be aligned to 4-byte boundaries per the ELF
        // spec. Calculate the aligned sizes by rounding up to the next multiple of 4.
        const size_t name_size_aligned = (nhdr->n_namesz + 3) & ~3;
        const size_t desc_size_aligned = (nhdr->n_descsz + 3) & ~3;

        // Validate that this note entry (header + aligned name + aligned desc) fits
        // within the buffer we read; if not, the note segment is malformed so abort
        // parsing
        if (offset + name_size_aligned + desc_size_aligned > note_segment_size) {
          break;
        }

        // Extract pointer to the note name string (should be "GNU" for GNU build ID
        // notes)
        const char* note_name = note_data + offset;
        offset += name_size_aligned;

        // Extract pointer to the note descriptor (the actual build ID bytes)
        const uint8_t* note_desc = reinterpret_cast<const uint8_t*>(note_data + offset);
        offset += desc_size_aligned;

        // GNU build ID notes have type NT_GNU_BUILD_ID (3), name "GNU\0" (4 bytes
        // including null terminator), and descriptor containing the raw build ID bytes
        // (typically 20 bytes for SHA-1)
        if (nhdr->n_type == NT_GNU_BUILD_ID && nhdr->n_namesz == 4 &&
            memcmp(note_name, "GNU", 4) == 0) {
          // Format the build ID as a lowercase hex string, with 2 hex characters per
          // byte
          size_t out_idx = 0;
          for (size_t byte_idx = 0;
               byte_idx < nhdr->n_descsz && out_idx <= buffer_size - 3;
               ++byte_idx) {
            // Ensure room for 2 hex chars + null terminator
            snprintf(
                out_buffer + out_idx, buffer_size - out_idx, "%02x", note_desc[byte_idx]
            );
            out_idx += 2;
          }
          out_buffer[out_idx] = '\0';

          return true;
        }
      }
    }
  }

  // We've iterated over all program headers without finding a GNU build ID note: there
  // is no build ID encoded in this file
  return false;
}

/**
 * Extracts ELF build ID from a Linux binary file.
 *
 * Opens the ELF file at `module_path`, reads program headers to find PT_NOTE
 * segments, parses note entries to find the NT_GNU_BUILD_ID note, and formats the
 * build ID as lowercase hex (e.g., "8c9d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d").
 *
 * Supports both 32-bit (Elf32_*) and 64-bit (Elf64_*) ELF files by detecting the ELF
 * class from e_ident[EI_CLASS].
 *
 * Returns true on success (build ID written to `out_buffer`), false on failure. This
 * function uses file I/O and is NOT async-signal-safe.
 */
static bool extract_elf_build_id(
    const char* module_path, char* out_buffer, size_t buffer_size
) {
  // Require a large enough output buffer to fit any ELF build ID: typical build IDs
  // are 20 bytes (40 hex chars) plus null terminator
  if (buffer_size < 41) {
    // Insufficient buffer size; abort
    return false;
  }

  // An ELF binary begins with an ELF header (Ehdr), which contains metadata about
  // the binary including the architecture (32-bit or 64-bit) and the location of the
  // program header table. The program header table is an array of program headers
  // (Phdr) that describe segments in the binary. One or more PT_NOTE segments may
  // contain note entries, which store auxiliary information about the binary.
  //
  // ELF file structure:
  // - 0x0000: [ELF Header (Ehdr) - 52 bytes (32-bit) or 64 bytes (64-bit)]
  //   - e_ident[16]: Magic bytes and format identifiers
  //   - e_phoff: Offset to program header table
  //   - e_phnum: Number of program headers
  // - e_phoff: [Program Header Table (Phdr array)]
  //   - Each Phdr describes a segment (loadable code/data, notes, etc.)
  //   - PT_NOTE segments (p_type == PT_NOTE) contain note entries
  // - (various offsets): [PT_NOTE segments]
  //   - Each note entry has: Nhdr (12 bytes) + name (aligned) + desc (aligned)
  //   - Nhdr fields: n_namesz, n_descsz, n_type
  //   - GNU build ID: n_type = NT_GNU_BUILD_ID (3), name = "GNU\0", desc = raw
  //   build ID bytes
  //
  // For example, a 64-bit ELF might have:
  // - 0x0000: ELF Header with e_ident[0..3] = 0x7F 'E' 'L' 'F'
  // - 0x0040: Program Header 0 (PT_LOAD)
  // - 0x0078: Program Header 1 (PT_NOTE) with p_offset = 0x0338, p_filesz = 0x44
  // - 0x0338: Note entry: Nhdr {n_namesz=4, n_descsz=20, n_type=3} + "GNU\0" + 20
  // build ID bytes

  // Open the ELF file for read
  int fd = open(module_path, O_RDONLY);
  if (fd < 0) {
    // Open failed; abort
    return false;
  }

  // The first 16 bytes of an ELF file (e_ident) are identical for both 32-bit and
  // 64-bit ELF files, containing magic bytes and format identifiers. Read these bytes
  // first so we can validate the file format and determine the architecture before
  // reading the architecture-specific header.
  unsigned char e_ident[EI_NIDENT];
  if (read(fd, e_ident, EI_NIDENT) != EI_NIDENT) {
    // Read failed; abort
    close(fd);
    return false;
  }

  // Validate the ELF magic bytes: 0x7F 'E' 'L' 'F' (0x7F454C46). These bytes
  // identify the file as an ELF binary.
  if (e_ident[EI_MAG0] != ELFMAG0 || e_ident[EI_MAG1] != ELFMAG1 ||
      e_ident[EI_MAG2] != ELFMAG2 || e_ident[EI_MAG3] != ELFMAG3) {
    // Invalid ELF magic; abort
    close(fd);
    return false;
  }

  // The EI_CLASS field in e_ident indicates whether this is a 32-bit (ELFCLASS32) or
  // 64-bit (ELFCLASS64) ELF file, which determines the size and layout of the ELF
  // header and program headers.
  const bool is_64bit = (e_ident[EI_CLASS] == ELFCLASS64);
  const bool is_32bit = (e_ident[EI_CLASS] == ELFCLASS32);

  if (!is_64bit && !is_32bit) {
    // Unsupported ELF class; abort
    close(fd);
    return false;
  }

  // Seek back to the start of the file so we can read the full architecture-specific
  // ELF header (Elf32_Ehdr or Elf64_Ehdr)
  if (lseek(fd, 0, SEEK_SET) != 0) {
    // Seek failed; abort
    close(fd);
    return false;
  }

  // Now that we've determined the architecture, read the full ELF header and call the
  // template helper to extract the build ID. The template is instantiated with the
  // appropriate architecture-specific types (Elf64_* or Elf32_*) to handle both 32-bit
  // and 64-bit ELF files with the same logic.
  bool success = false;
  if (is_64bit) {
    // Read 64-bit ELF header
    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) == sizeof(ehdr)) {
      success = extract_elf_build_id_from_headers<Elf64_Ehdr, Elf64_Phdr, Elf64_Nhdr>(
          fd, ehdr, out_buffer, buffer_size
      );
    }
    // Read failed; success remains false
  } else if (is_32bit) {
    // Read 32-bit ELF header
    Elf32_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) == sizeof(ehdr)) {
      success = extract_elf_build_id_from_headers<Elf32_Ehdr, Elf32_Phdr, Elf32_Nhdr>(
          fd, ehdr, out_buffer, buffer_size
      );
    }
    // Read failed; success remains false
  }

  close(fd);
  return success;
}

void PopulateBuildIdCache() {
  // Reset the cache to empty state before repopulating
  g_module_build_id_cache.num_entries = 0;

  // On Linux, /proc/self/maps is a virtual file that lists all memory mappings for the
  // current process. Each line describes one mapping with the format:
  //   start_addr-end_addr perms offset dev:inode pathname
  //
  // Example line:
  //   7f1234567000-7f1234568000 r-xp 00001000 08:01 12345
  //   /lib/x86_64-linux-gnu/libc.so.6
  //
  // We parse this file to enumerate all loaded binaries and libraries (executable
  // mappings with absolute pathnames), then extract their build IDs from the ELF files.
  FILE* maps = fopen("/proc/self/maps", "r");
  if (!maps) {
    // Failed to open maps file; cache remains empty
    return;
  }

  char line[4096];
  // Track which modules we've already cached (by pathname) to avoid duplicate entries.
  // A single binary/library typically has multiple memory mappings (code, data,
  // rodata), but we only need to cache the build ID once per unique pathname. Static
  // storage to avoid 64KB stack allocation.
  static char cached_paths[kMaxCachedModules][256];
  size_t num_cached_paths = 0;

  while (fgets(line, sizeof(line), maps) &&
         g_module_build_id_cache.num_entries < kMaxCachedModules) {
    uintptr_t start_addr = 0;
    uintptr_t end_addr = 0;
    char perms[5] = {};
    char pathname[256] = {};

    // Parse each line to extract: start address, end address, permissions, and
    // pathname. The format is: %lx-%lx %4s %*x %*x:%*x %*d %255s where %*x means "read
    // but discard" for offset, device, and inode fields we don't need.
    // SCNxPTR is used instead of "lx" to correctly match uintptr_t on all platforms.
    if (sscanf(
            line,
            "%" SCNxPTR "-%" SCNxPTR " %4s %*x %*x:%*x %*d %255s",
            &start_addr,
            &end_addr,
            perms,
            pathname
        ) == 4) {
      // Only process executable segments (perms contains 'x') with absolute paths
      // (pathname starts with '/'). These are binaries and libraries that may have
      // build IDs; other mappings (stack, heap, anonymous, relative paths) do not.
      if (strchr(perms, 'x') && pathname[0] == '/') {
        // Check if we've already cached this module's build ID to avoid duplicate
        // entries. A single binary typically has multiple mappings (text, data, etc.),
        // but we only need one cache entry per unique file.
        bool already_cached = false;
        for (size_t i = 0; i < num_cached_paths; ++i) {
          if (strcmp(cached_paths[i], pathname) == 0) {
            already_cached = true;
            break;
          }
        }

        if (!already_cached && num_cached_paths < kMaxCachedModules) {
          // Module not yet cached; attempt to extract the build ID from the ELF file on
          // disk
          char build_id[kMaxBuildIdLength];
          if (extract_elf_build_id(pathname, build_id, sizeof(build_id))) {
            // Successfully extracted build ID: add this module to the cache, recording
            // its base address (where the first mapping starts) and build ID string
            const size_t entry_idx = g_module_build_id_cache.num_entries;
            auto& cached = g_module_build_id_cache.entries[entry_idx];
            cached.base_address = start_addr;
            snprintf(cached.build_id, kMaxBuildIdLength, "%s", build_id);
            cached.valid = true;
            // Make entry visible to readers only after fully written
            g_module_build_id_cache.num_entries = entry_idx + 1;

            // Track this pathname so we can skip subsequent mappings of the same binary
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

void PopulateBuildIdCache() {
  // Not needed on macOS—build IDs extracted from memory at crash time
}

#endif  // __APPLE__

const char* FindCachedBuildId(uintptr_t base_address) {
  // Linear search through cache (async-signal-safe)
  const size_t count = g_module_build_id_cache.num_entries;
  for (size_t i = 0; i < count; ++i) {
    const CachedModuleBuildId& entry = g_module_build_id_cache.entries[i];
    if (entry.valid && entry.base_address == base_address) {
      return entry.build_id;
    }
  }
  return nullptr;
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(bugprone-unchecked-string-to-number-conversion)

}  // namespace datadog::impl
