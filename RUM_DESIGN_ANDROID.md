# RUM Design for Datadog C++ SDK

## Overview

This document outlines the design for implementing Real User Monitoring (RUM) functionality in the Datadog C++ SDK. Unlike mobile SDKs that provide automatic UI framework integration, the C++ SDK focuses on explicit RUM tracking APIs that application developers can use to manually instrument their applications.

The first-pass implementation will focus on three core RUM concepts: Sessions, Views, and Actions.

## Core Design Principles

1. **Explicit Tracking**: All RUM events must be explicitly tracked by the application developer
2. **Framework Agnostic**: No assumptions about UI frameworks or application structure
3. **Thread Safety**: All APIs must be thread-safe for multi-threaded applications
4. **Resource Efficient**: Minimal overhead when RUM is disabled or not in use
5. **Extensible**: Design should allow for future expansion to include Resources and Errors

## 1. RUM Sessions

### Purpose
Sessions represent a user's interaction period with the application. A session groups all RUM events (views, actions, etc.) that occur during a continuous usage period.

### API Design

```cpp
class RumMonitor {
public:
    // Get current session ID (returns empty string if no active session)
    std::string getCurrentSessionId() const;
    
    // Manually stop current session
    void stopSession();
    
    // Sessions start automatically with first view/action
    // No explicit startSession() needed
};
```

### Implementation Details

**Session State Management:**
- Session starts automatically when first view or action is tracked
- Session ID generated as UUID v4
- Session timeout after configurable period of inactivity (default: 15 minutes)
- Session sampling based on configuration (0-100%)

**Session Storage:**
- Current session ID stored in thread-safe member variable
- Session start timestamp tracked
- Last activity timestamp updated on each RUM event

**Session Events:**
- Generate session start event when new session begins
- Generate session end event when session stops (timeout or manual)
- Include session duration in session end events

### Configuration
```cpp
struct RumSessionConfig {
    std::chrono::minutes session_timeout{15};
    double sample_rate{1.0}; // 0.0 to 1.0
    bool enabled{true};
};
```

## 2. RUM Views

### Purpose
Views represent distinct screens or pages in the application. In C++ applications, these could represent different application states, UI windows, or logical application sections.

### API Design

```cpp
enum class ViewEventType {
    START,
    STOP
};

class RumMonitor {
public:
    // Start tracking a new view
    void startView(const std::string& view_key, 
                   const std::string& view_name,
                   const std::map<std::string, std::string>& attributes = {});
    
    // Stop tracking current view
    void stopView(const std::string& view_key,
                  const std::map<std::string, std::string>& attributes = {});
    
    // Add custom timing to current view
    void addTiming(const std::string& name);
    
    // Get current active view (returns empty string if none)
    std::string getCurrentViewId() const;
};
```

### Implementation Details

**View State Management:**
- Track current active view with unique view ID (UUID v4)
- Support view stack for nested views (optional for first implementation)
- View key serves as user-provided identifier for correlation
- View name serves as human-readable identifier

**View Lifecycle:**
- `startView()` creates new view with unique ID and start timestamp
- `stopView()` marks view as ended with end timestamp and duration
- Only one view can be active at a time (simple implementation)
- Starting new view automatically stops previous view

**View Attributes:**
- Support custom attributes at view start and stop
- Merge attributes from start and stop events
- Validate attribute keys and values (sanitize special characters)

**View Events:**
- Generate view start event immediately when `startView()` called
- Generate view stop event when `stopView()` called or new view started
- Include view duration, action count, error count in stop events

### View Data Structure
```cpp
struct ViewInfo {
    std::string view_id;
    std::string view_key;
    std::string view_name;
    std::chrono::steady_clock::time_point start_time;
    std::map<std::string, std::string> attributes;
    std::vector<std::string> custom_timings;
    int action_count{0};
};
```

## 3. RUM Actions

### Purpose
Actions represent user interactions or application events within a view. Actions can be instantaneous (like button clicks) or have duration (like form submissions).

### API Design

```cpp
enum class ActionType {
    CUSTOM,
    CLICK,
    SUBMIT,
    NAVIGATION,
    LOAD
};

class RumMonitor {
public:
    // Add instantaneous action
    void addAction(ActionType type,
                   const std::string& name,
                   const std::map<std::string, std::string>& attributes = {});
    
    // Start long-running action
    std::string startAction(ActionType type,
                           const std::string& name,
                           const std::map<std::string, std::string>& attributes = {});
    
    // Stop long-running action
    void stopAction(const std::string& action_id,
                    const std::map<std::string, std::string>& attributes = {});
    
    // Stop action with updated type/name
    void stopAction(const std::string& action_id,
                    ActionType type,
                    const std::string& name,
                    const std::map<std::string, std::string>& attributes = {});
};
```

### Implementation Details

**Action Types:**
- `CUSTOM`: Application-defined action
- `CLICK`: Button/element click
- `SUBMIT`: Form submission or data commit
- `NAVIGATION`: Navigation between views/states
- `LOAD`: Data loading operation

**Action Lifecycle:**
- `addAction()` creates complete action event immediately
- `startAction()` returns action ID for later correlation
- `stopAction()` completes action with duration calculation
- Actions automatically timeout after 10 seconds if not stopped

**Action State Management:**
- Track active actions in thread-safe container
- Action ID generated as UUID v4
- Associate actions with current active view
- Clean up orphaned actions on view changes

**Action Events:**
- Instantaneous actions generate single action event
- Long-running actions generate start event and completion event
- Include action duration for completed actions
- Increment action count in parent view

### Action Data Structure
```cpp
struct ActionInfo {
    std::string action_id;
    ActionType type;
    std::string name;
    std::chrono::steady_clock::time_point start_time;
    std::map<std::string, std::string> attributes;
    std::string parent_view_id;
    bool completed{false};
};
```

## Cross-Cutting Concerns

### Thread Safety
- Use `std::mutex` to protect shared state
- Consider `std::shared_mutex` for read-heavy operations
- Atomic operations for simple counters and flags

### Event Generation
- RUM events serialized to JSON format
- Events queued for batch transmission
- Include common fields: timestamp, session_id, view_id, application_id

### Configuration
```cpp
struct RumConfig {
    std::string application_id;
    std::string client_token;
    std::string environment;
    std::string service;
    std::string version;
    RumSessionConfig session;
    bool enabled{true};
};
```

### Error Handling
- Graceful degradation when RUM disabled
- Validate input parameters and log warnings for invalid data
- No exceptions thrown from public APIs
- Return success/failure indicators where appropriate

### Memory Management
- Use RAII principles
- Automatic cleanup of expired sessions and actions
- Configurable limits on stored events and state

## Implementation Priority

1. **Phase 1**: Basic session management and single view tracking
2. **Phase 2**: Action tracking with instantaneous actions only
3. **Phase 3**: Long-running actions and action timeout handling
4. **Phase 4**: Advanced features (custom timings, attribute validation)

This design provides a solid foundation for RUM functionality in C++ applications while maintaining flexibility for future enhancements.
