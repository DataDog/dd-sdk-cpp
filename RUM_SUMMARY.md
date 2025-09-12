# RUM Implementation Summary for Datadog C++ SDK

## RUM Feature Set Overview

Real User Monitoring (RUM) provides application performance monitoring by tracking user interactions and application behavior in real-time. The C++ SDK implementation focuses on three core concepts:

### Core RUM Concepts

1. **Sessions**: Represent continuous user interaction periods with automatic timeout and sampling support
2. **Views**: Represent distinct screens, pages, or application states with custom attributes and timing metrics
3. **Actions**: Represent user interactions (clicks, navigation, loading) that can be either instantaneous or duration-based

### Key Design Principles

- **Explicit Tracking**: No automatic UI framework integration - developers manually instrument their applications
- **Framework Agnostic**: Works with any C++ application regardless of UI framework
- **Thread Safety**: All APIs designed for multi-threaded applications
- **Resource Efficient**: Minimal overhead when disabled, configurable sampling
- **Event-Driven**: Generates structured events sent to Datadog intake for analysis

## Scope of Work Summary

The four work items represent a complete first-pass RUM implementation:

### RUM-11366: Foundation (Do-Nothing API)
- Establishes basic RUM feature infrastructure
- Creates C and C++ API surface with no functional implementation
- Sets up module structure following SDK conventions
- Enables RUM feature registration in example applications

### RUM-11367: Session Management
- Implements session lifecycle with UUID generation
- Adds session sampling and timeout logic
- Provides session state tracking for future event correlation
- Creates internal session event generation (not yet uploaded)

### RUM-11368: View Tracking with Upload
- Implements view lifecycle management with custom attributes
- Adds custom timing support for performance measurement  
- **First work item to actually send events to Datadog intake**
- Establishes full upload pipeline integration

### RUM-11369: Action Tracking
- Implements both discrete and continuous action types
- Adds action-view association and timeout handling
- Includes error count tracking during action duration
- Completes the core RUM feature set

## Integration with dd-sdk-cpp Codebase

### Architectural Fit

The RUM implementation follows established SDK patterns:

**Feature Architecture**:
- `src/impl/features/rum/` - Core implementation following `features/logging/` pattern
- Inherits from `Feature` base class with `GetId()`, `GetName()`, `UploadThread_PrepareReport()`
- Uses existing event storage and batch upload infrastructure

**API Structure**:
- `include-c/datadog/rum.h` - C API following logging conventions
- `include-cpp/datadog/rum.hpp` - C++ API with RAII and modern patterns
- `src/c/rum.cpp` and `src/cpp/rum.cpp` - API bindings to internal implementation

**Integration Points**:
- Uses existing `Core` class for feature registration
- Leverages existing `Attribute` system for custom properties
- Integrates with existing HTTP client for intake uploads
- Uses existing TLV storage format for event persistence

### Code Organization

```
src/impl/features/rum/
├── rum.hpp          # Main RUM feature class
├── rum.cpp          # Implementation with session/view/action logic  
├── types.hpp        # RUM-specific data structures and enums
└── (future)         # Error tracking, resource monitoring, etc.

include-c/datadog/
├── rum.h           # C API declarations

include-cpp/datadog/  
├── rum.hpp         # C++ API declarations

src/c/
├── rum.cpp         # C API implementation

src/cpp/
├── rum.cpp         # C++ API implementation

examples/
├── c/main.c        # Updated with RUM usage
└── cpp/main.cpp    # Updated with RUM usage

tests/
├── c/rum_c_api_test.cpp     # C API tests
└── cpp/rum_cpp_api_test.cpp # C++ API tests
```

### Dependencies and Constraints

**New Dependencies**:
- UUID generation library (for session/view/action IDs)
- JSON serialization for RUM events (may leverage existing attribute JSON support)

**Existing SDK Dependencies**:
- Core feature registration system
- TLV event storage and batching
- HTTP client for upload
- Platform abstractions (clock, threading)
- Attribute system for custom properties

## Time Estimates

### RUM-11366: Foundation (Do-Nothing API)
**Estimated Time: 2-3 days**

- **Complexity**: Low - mostly scaffolding and following existing patterns
- **Key Tasks**: 
  - Create directory structure (0.5 day)
  - Implement basic Feature class (0.5 day)  
  - Create C/C++ API headers (0.5 day)
  - Implement API bindings (0.5 day)
  - Update examples and tests (0.5 day)
  - Build system integration (0.5 day)

- **Risk Factors**: Low - straightforward implementation following logging patterns

### RUM-11367: Session Management
**Estimated Time: 4-5 days**

- **Complexity**: Medium - requires UUID generation, sampling, and state management
- **Key Tasks**:
  - Implement session data structures (0.5 day)
  - Add UUID generation utility (0.5 day)
  - Implement session lifecycle logic (1 day)
  - Add session sampling and timeout (1 day)
  - Create session event generation (0.5 day)
  - Update APIs and examples (1 day)
  - Comprehensive testing (0.5-1 day)

- **Risk Factors**: Medium - UUID library integration, thread safety complexity

### RUM-11368: View Tracking with Upload  
**Estimated Time: 6-8 days**

- **Complexity**: High - first work item requiring full upload integration
- **Key Tasks**:
  - Implement view data structures and lifecycle (1.5 days)
  - Create view event generation and JSON formatting (1 day)  
  - **Implement upload pipeline integration (2-3 days)**
  - Add custom timing and attribute support (1 day)
  - Update APIs, examples, and tests (1.5 days)
  - Integration testing with actual intake (1 day)

- **Risk Factors**: High - upload integration complexity, intake endpoint research, event format validation

### RUM-11369: Action Tracking
**Estimated Time: 5-6 days**

- **Complexity**: Medium-High - complex action lifecycle with timeout handling
- **Key Tasks**:
  - Implement action types and data structures (1 day)
  - Create discrete action handling (0.5 day)
  - Implement continuous action lifecycle (1.5 days)
  - Add action timeout and cleanup logic (1 day)
  - Implement action-view integration (0.5 day)
  - Add error tracking stubs (0.5 day)
  - Update APIs, examples, and comprehensive testing (1-1.5 days)

- **Risk Factors**: Medium - action timeout complexity, concurrent action management

## Total Estimated Timeline: 17-22 days

### Dependency Chain
- **RUM-11366** → **RUM-11367** → **RUM-11368** → **RUM-11369**
- Each work item builds on the previous one
- RUM-11368 is the critical path item due to upload integration complexity
- Parallelization not possible due to dependencies

### Key Success Factors

1. **UUID Library Integration**: Selecting and integrating appropriate UUID generation
2. **Upload Pipeline**: Correctly implementing Datadog RUM intake integration  
3. **Thread Safety**: Ensuring robust concurrent access patterns
4. **Event Format**: Matching Datadog's expected RUM event schema
5. **Testing Coverage**: Comprehensive testing including integration tests

### Future Extension Points

The implementation provides foundation for future RUM enhancements:
- Resource tracking for network requests
- Error reporting and crash tracking  
- Performance vitals and core web vitals
- Advanced sampling strategies
- Custom event mappers and processors

This phased approach ensures a solid, production-ready RUM implementation while maintaining extensibility for advanced features.
