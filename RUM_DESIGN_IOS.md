# RUM API Design for Datadog C++ SDK

This document outlines the design for implementing Real User Monitoring (RUM) APIs in the Datadog C++ SDK, focusing on the essential components: Sessions, Views, and Actions.

## Overview

The RUM feature provides application performance monitoring by tracking user sessions, screen interactions, and user actions. Unlike mobile SDKs with automatic UI framework integration, the C++ SDK will provide explicit APIs for manual instrumentation.

## Core Architecture

### 1. RUM Context Management
RUM uses a hierarchical scope model:
- **Application Scope** (singleton) - manages the overall RUM feature
- **Session Scope** - manages user sessions with timeout and sampling
- **View Scope** - manages individual screen/page views
- **Action Scope** - manages user interactions within views

### 2. UUID Management
Each RUM entity requires a unique identifier:
- Use 128-bit UUIDs for all RUM entities
- Support "null UUID" (all zeros) for sampled-out sessions
- Generate unique UUIDs per session, view, and action

### 3. Threading Model
- Thread-safe API design for concurrent access
- Internal synchronization for scope management
- Background thread for event processing and transmission

## Implementation Tasks

## Task 1: Implement RUM Sessions

### 1.1 Session Lifecycle Management

**Session State:**
```cpp
class RUMSession {
    std::string session_id;
    bool is_sampled;
    bool is_active;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point last_interaction_time;
    bool has_tracked_any_view;
};
```

**Session Configuration:**
- **Session Timeout**: 15 minutes of inactivity (configurable)
- **Session Max Duration**: 4 hours maximum (configurable) 
- **Sampling Rate**: 0-100% session sampling (default: 100%)
- **End Reasons**: timeout, max_duration, manual_stop

**Core Session APIs:**
```cpp
class RUMMonitor {
public:
    // Automatic session management - sessions start on first view/action
    void start_session();
    void stop_session();
    std::string get_current_session_id();
    bool is_session_active();
    
    // Session configuration
    void set_session_sample_rate(float rate);  // 0.0 - 100.0
    void set_session_timeout(std::chrono::minutes timeout);
    void set_session_max_duration(std::chrono::hours max_duration);
};
```

### 1.2 Session Sampling
- Sample sessions based on configured rate
- Sampled-out sessions use null UUID and don't send events
- Maintain session sampling decision throughout session lifecycle

### 1.3 Session Context
- Track session metadata (start time, sampling status, view count)
- Provide session context to child views and actions
- Handle session transitions and cleanup

## Task 2: Implement RUM Views

### 2.1 View Lifecycle Management

**View State:**
```cpp
class RUMView {
    std::string view_id;
    std::string view_name;
    std::string view_path;
    std::chrono::system_clock::time_point start_time;
    std::optional<std::chrono::system_clock::time_point> loading_time;
    std::map<std::string, AttributeValue> attributes;
    std::map<std::string, int64_t> custom_timings; // nanoseconds
    bool is_active;
    
    // Counters
    uint32_t actions_count;
    uint32_t resources_count;
    uint32_t errors_count;
    uint32_t long_tasks_count;
};
```

**Core View APIs:**
```cpp
class RUMMonitor {
public:
    // View lifecycle - key-based tracking
    void start_view(const std::string& view_key, 
                   const std::string& view_name = "",
                   const AttributeMap& attributes = {});
    
    void stop_view(const std::string& view_key,
                  const AttributeMap& attributes = {});
    
    // View enhancements  
    void add_view_loading_time(bool overwrite = false);
    void add_timing(const std::string& name);
    
    // View context
    std::string get_active_view_id();
    std::string get_active_view_name();
};
```

### 2.2 View Management Rules
- Multiple views can be active simultaneously
- View names default to view keys if not specified
- Views automatically associate with current session
- Views can outlive sessions (for session transitions)

### 2.3 View Events Generation
- Generate view start events immediately
- Generate view end events with metrics (duration, counts)
- Include view loading time and custom timings
- Attach session and application context

### 2.4 Custom Timings
- Support custom performance markers within views
- Store timing as nanoseconds from view start
- Unique timing names per view
- Include in view end events

## Task 3: Implement RUM Actions

### 3.1 Action Types and Lifecycle

**Action Types:**
```cpp
enum class RUMActionType {
    TAP,
    CLICK, 
    SCROLL,
    SWIPE,
    CUSTOM
};
```

**Action State:**
```cpp
class RUMAction {
    std::string action_id;
    RUMActionType action_type;
    std::string action_name;
    std::chrono::system_clock::time_point start_time;
    std::map<std::string, AttributeValue> attributes;
    bool is_continuous;
    std::chrono::system_clock::time_point last_activity_time;
    
    // Counters during action lifecycle
    uint32_t resources_count;
    uint32_t errors_count;
    uint32_t long_tasks_count;
};
```

**Core Action APIs:**
```cpp
class RUMMonitor {
public:
    // Discrete (instantaneous) actions
    void add_action(RUMActionType type,
                   const std::string& name,
                   const AttributeMap& attributes = {});
    
    // Continuous (duration-based) actions  
    void start_action(RUMActionType type,
                     const std::string& name, 
                     const AttributeMap& attributes = {});
    
    void stop_action(RUMActionType type,
                    const std::string& name = "",
                    const AttributeMap& attributes = {});
};
```

### 3.2 Action Behavior Rules
- **Discrete Actions**: Instantaneous events (tap, click)
- **Continuous Actions**: Duration-based events (scroll, swipe)
- **Action Timeout**: Continuous actions auto-end after 10 seconds
- **Single Active Action**: Only one continuous action per view
- **Activity Tracking**: Actions track associated resources/errors

### 3.3 Action-View Association
- Actions must be associated with an active view
- If no view is active, create a "background" view (if enabled)
- Actions inherit view and session context
- Action events include parent view information

### 3.4 Action Auto-Completion
- Continuous actions auto-complete after max duration (10s)
- Discrete actions complete after brief timeout (100ms) or next activity
- Track resources and errors that occur during action lifecycle

## Data Types and Utilities

### Attribute Management
```cpp
using AttributeValue = std::variant<std::string, int64_t, double, bool>;
using AttributeMap = std::map<std::string, AttributeValue>;
```

### Event Structure
RUM events should include:
- Event type (`action`, `view`, `session`)
- Unique event ID
- Timestamp (milliseconds since epoch)
- Session context (session ID, sampling status)
- View context (view ID, name, path) 
- Application context (app ID, version)
- Device/environment context
- Custom attributes

### Configuration
```cpp
struct RUMConfiguration {
    std::string application_id;
    float session_sample_rate = 100.0f;
    std::chrono::minutes session_timeout{15};
    std::chrono::hours session_max_duration{4};
    bool track_background_events = false;
    std::string custom_endpoint; // optional
};
```

## Implementation Guidelines

### Error Handling
- Graceful degradation when RUM is not properly initialized
- No-op implementations for sampled-out sessions
- Validation of input parameters (names, attributes)
- Thread-safe operations with appropriate locking

### Performance Considerations
- Minimize allocation overhead in hot paths
- Use move semantics for attribute maps
- Batch event processing where possible
- Lazy evaluation of event generation

### Testing Strategy
- Unit tests for each scope lifecycle
- Integration tests for scope interactions
- Threading tests for concurrent access
- Sampling validation tests
- Performance benchmarks

### Dependencies
- UUID generation library
- High-resolution timing (chrono)
- Thread synchronization primitives
- JSON serialization for events
- HTTP client for event transmission

## Future Extensions

This initial implementation focuses on core functionality. Future enhancements may include:
- Resource tracking for network requests
- Error tracking and reporting  
- Long task detection
- Performance vitals collection
- Custom event mappers
- Advanced sampling strategies

The design provides a solid foundation for essential RUM functionality while allowing for future feature expansion.
