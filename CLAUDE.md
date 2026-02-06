# CLAUDE.md - AI Assistant Guidelines

This document defines the software engineering practices for building this audio engine with AI assistance. Follow these guidelines to maintain code quality, architectural integrity, and avoid common pitfalls of AI-assisted development.

---

## Core Philosophy

**Treat AI as a fast but judgment-limited junior developer.** You (the human) provide judgment through specifications, tests, and review. The AI provides implementation velocity. Neither works well alone.

**Design top-down, build bottom-up.** The architecture document defines component boundaries. Implementation proceeds one component at a time, bottom-up through the dependency tree.

**Specifications and tests are the source of truth.** Code is derived from specs. Tests enforce specs. If code and spec disagree, the code is wrong.

---

## The Build Cycle for Each Component

### Step 1: Write the Specification

Before any code, create a specification document in `docs/specs/{Component}.md`.

**Specification template:**

```markdown
# {Component} Specification

## Responsibilities
- What this component does (bullet points)
- Keep it focused—if the list is long, consider splitting

## Interface
- Public methods with signatures
- Use pseudocode or C++ declarations
- Include parameter and return types

## Invariants
- Properties that must always be true
- These become test cases

## Error Conditions
- What can fail and how failures are reported
- These become test cases

## Does NOT Handle
- Explicitly list out-of-scope concerns
- Prevents scope creep during implementation

## Dependencies
- What other components this one uses
- Must be components that already exist or are mocked

## Thread Safety
- Which thread(s) call which methods
- What synchronization is required (or explicitly: none)

## Example Usage
- Short code snippet showing typical use
```

**AI prompt for spec review:**

> "Review this specification for {Component}. Check for: ambiguities, missing error conditions, unstated invariants, scope creep, and interface clarity. Suggest improvements."

### Step 2: Write Tests from the Specification

Create test file `tests/{area}/{Component}Tests.cpp` before implementation.

**Test categories to cover:**

1. **Happy path**: Normal usage works correctly
2. **Invariants**: Properties from spec hold under all conditions
3. **Error conditions**: Failures are detected and reported correctly
4. **Edge cases**: Empty inputs, maximum sizes, boundary conditions
5. **Does NOT handle**: Verify component doesn't overstep (where testable)

**AI prompt for test generation:**

> "Based on this specification for {Component}, generate a comprehensive test file using [Catch2/GoogleTest]. Cover all invariants, error conditions, and edge cases. Use descriptive test names that reference the spec."

**Review checklist for generated tests:**

- [ ] Every invariant from spec has at least one test
- [ ] Every error condition from spec has a test
- [ ] Tests are independent (no shared mutable state)
- [ ] Test names describe what's being tested, not how
- [ ] No implementation assumptions leaked into tests

### Step 3: Implement to Pass Tests

**AI prompt for implementation:**

> "Implement {Component} to pass these tests. Here is the specification: [spec]. Here is the header file: [header]. Follow these constraints:
> - No additional public methods beyond the spec
> - Match the error handling described in the spec
> - Add private helper methods as needed
> - Include comments only for non-obvious logic"

**Review checklist for generated implementation:**

- [ ] All tests pass
- [ ] Public interface matches spec exactly
- [ ] No functionality beyond what's specified
- [ ] Realtime safety (if applicable): no allocation, no blocking, no unbounded loops
- [ ] Code is readable without excessive comments
- [ ] No obvious performance issues

### Step 4: Integration Check

After implementation, verify the component works with its dependencies:

- If dependencies were mocked, write integration tests with real components
- Run the full test suite to catch regressions
- Update the architecture document if any decisions were made

---

## Component Build Order

Build in dependency order. A component can only depend on components above it in this list:

```
1. Port                 (no dependencies)
2. Node                 (depends on Port)
3. Graph                (depends on Node, Port)
4. TestNodes            (depends on Node) - GainNode, PassthroughNode, etc.
5. Scheduler            (no dependencies on other components)
6. Transport            (no dependencies on other components)
7. Meters               (depends on Node)
8. Engine               (depends on Graph, Scheduler, Transport, Meters)
9. PluginNode           (depends on Node, JUCE plugin hosting)
10. LuaBindings         (depends on all above)
11. ServerAPI           (depends on LuaBindings or Engine directly)
12. WebSocketServer     (depends on ServerAPI)
13. OSCServer           (depends on ServerAPI)
```

**Do not skip ahead.** Each component must be complete (spec, tests, implementation, review) before starting the next.

---

## Mocking Strategy

When building a component, mock its dependencies rather than waiting for them to exist.

**Mock requirements:**

- Mocks implement the same interface as the real component
- Mocks record calls for verification
- Mocks can be configured to return specific values or trigger errors
- Mocks are simple—no logic beyond recording and playback

**Example:**

```cpp
// Mock for testing Scheduler before Engine exists
class MockCommandExecutor : public ICommandExecutor {
public:
    std::vector<Command> executed;
    
    void execute(const Command& cmd) override {
        executed.push_back(cmd);
    }
};

TEST_CASE("Scheduler queues commands") {
    MockCommandExecutor executor;
    Scheduler scheduler(&executor);
    
    scheduler.schedule(Command::AddNode{...});
    scheduler.processPending();
    
    REQUIRE(executor.executed.size() == 1);
}
```

**When to replace mocks with real implementations:**

- After both components are implemented
- Write integration tests that use real components
- Keep unit tests with mocks for fast, isolated testing

---

## Realtime Safety Rules

The audio thread has strict constraints. Any code that runs on the audio thread must follow these rules:

### Forbidden on Audio Thread

- `new` / `delete` / `malloc` / `free`
- `std::vector::push_back` (may allocate)
- `std::map` / `std::unordered_map` (allocate on insert)
- `std::mutex` / `std::lock_guard` (blocks)
- `std::condition_variable` (blocks)
- File I/O
- Network I/O
- `std::cout` / logging (may block or allocate)
- Any STL container operation that might allocate
- Virtual function calls through unknown code (plugins are the exception)

### Allowed on Audio Thread

- Lock-free queues (SPSC specifically)
- Atomic operations
- Fixed-size arrays / `std::array`
- Pre-allocated buffers
- Arithmetic, SIMD operations
- Calling `process()` on nodes (they must also be RT-safe)

### Testing for Realtime Safety

Where possible, use tools to verify:

- Static analysis for allocation detection
- Runtime detection in debug builds (override global new/delete to assert if called from audio thread)

**AI prompt for RT-safe code:**

> "Implement this function to be realtime-safe. It will be called from the audio thread. No allocation, no blocking, no unbounded iteration. Use only pre-allocated buffers and lock-free primitives."

---

## AI Interaction Guidelines

### Prompt Structure

Always provide context in this order:

1. **Architecture context**: Relevant excerpt from ARCHITECTURE.md
2. **Specification**: The component's spec document
3. **Existing code**: Headers, related implementations
4. **Specific task**: What you want the AI to do
5. **Constraints**: What to avoid, style requirements

### Prompt Templates

**Spec review:**
```
Review this specification for clarity and completeness:

[paste spec]

Check for:
- Ambiguous requirements
- Missing error conditions
- Unstated invariants
- Interface clarity
- Scope creep
```

**Test generation:**
```
Generate tests for this component.

Specification:
[paste spec]

Header:
[paste header]

Use [Catch2/GoogleTest]. Cover:
- All invariants
- All error conditions
- Edge cases (empty, maximum, boundary)

Name tests descriptively.
```

**Implementation:**
```
Implement this component to pass the tests.

Specification:
[paste spec]

Header:
[paste header]

Tests:
[paste tests or summary]

Constraints:
- Match spec exactly, no extra functionality
- [Realtime-safe / Thread-safe / etc.]
- Minimal comments, only for non-obvious logic
```

**Code review request:**
```
Review this implementation for:
- Correctness against the spec
- Realtime safety (if applicable)
- Performance issues
- Unnecessary complexity
- Missing error handling

Spec:
[paste spec]

Implementation:
[paste code]
```

### Red Flags in AI Output

Watch for these issues in generated code:

- **Scope creep**: Features not in the spec
- **Over-engineering**: Unnecessary abstractions, premature optimization
- **Incorrect error handling**: Silent failures, wrong error types
- **Thread safety mistakes**: Missing atomics, wrong lock scope
- **Allocation in RT code**: std::vector, std::string, std::map in audio path
- **Untested paths**: Code that no test exercises
- **Magic numbers**: Unexplained constants
- **Copy-paste artifacts**: Duplicate code, inconsistent naming

### When to Reject and Regenerate

- Tests don't pass
- Public interface doesn't match spec
- Realtime safety violations (if applicable)
- Code is significantly more complex than the problem requires
- You don't understand what the code does after reading it

### When to Iterate vs. Rewrite

**Iterate** (ask for specific fixes) when:
- Most of the code is good
- Issues are localized
- You can describe the fix clearly

**Rewrite** (new prompt from scratch) when:
- Fundamental approach is wrong
- Too many scattered issues
- The code is confusing

---

## Documentation Requirements

### Code Documentation

- **Headers**: Brief doc comment for each public method
- **Implementation**: Comments only for non-obvious logic
- **No redundant comments**: Don't restate what the code clearly does

### Decision Log

When making non-obvious decisions during implementation, record them in ARCHITECTURE.md:

```markdown
## Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2024-01-15 | Graph uses adjacency list | Sparse graphs expected, simpler than matrix |
| 2024-01-16 | Scheduler uses SPSC queue | Single producer/consumer pattern fits our thread model |
```

### Spec Updates

If implementation reveals spec issues:

1. Stop implementation
2. Update the spec
3. Update tests if needed
4. Continue implementation

Never let code diverge from spec—the spec is the source of truth.

---

## Testing Standards

### Test Organization

```
tests/
├── core/                    # Unit tests for core components
│   ├── PortTests.cpp
│   ├── NodeTests.cpp
│   └── ...
├── nodes/                   # Unit tests for node implementations
│   └── ...
├── integration/             # Tests spanning multiple components
│   ├── EngineGraphTests.cpp
│   └── ...
└── mocks/                   # Shared mock implementations
    ├── MockNode.h
    └── ...
```

### Test Naming

Use descriptive names that read as specifications:

```cpp
// Good
TEST_CASE("Graph rejects connections that would create cycles")
TEST_CASE("Scheduler processes commands in order")
TEST_CASE("Transport position advances by exactly block size when playing")

// Bad
TEST_CASE("test1")
TEST_CASE("cycle test")
TEST_CASE("transport works")
```

### Test Independence

- Each test creates its own fixtures
- No shared mutable state between tests
- Tests can run in any order
- Tests can run in parallel

### Coverage Goals

- All public methods exercised
- All error paths exercised
- All invariants verified
- Edge cases for numeric inputs (0, 1, max, overflow)
- Edge cases for collections (empty, single, many)

---

## Git Workflow

### Commit Granularity

One component per branch/PR:

```
feature/port-component
feature/node-component
feature/graph-component
```

### Commit Message Format

```
{component}: {brief description}

- Detailed point 1
- Detailed point 2

Refs: #{issue} (if applicable)
```

### PR Checklist

Before merging:

- [ ] Spec document exists and is complete
- [ ] All tests pass
- [ ] Test coverage meets standards
- [ ] Code reviewed (by human, not just AI)
- [ ] No warnings from static analysis
- [ ] ARCHITECTURE.md updated if decisions were made
- [ ] No TODO comments (create issues instead)

---

## Quick Reference

### Starting a New Component

1. `docs/specs/{Component}.md` — write spec
2. Review spec (AI + human)
3. `tests/{area}/{Component}Tests.cpp` — write tests
4. Review tests
5. `src/{area}/{Component}.h` — write header
6. `src/{area}/{Component}.cpp` — implement
7. Run tests, iterate until passing
8. Human review of implementation
9. Integration tests if applicable
10. Update ARCHITECTURE.md decisions log

### AI Prompt Checklist

- [ ] Included architecture context
- [ ] Included component spec
- [ ] Included relevant existing code
- [ ] Stated task clearly
- [ ] Listed constraints
- [ ] Specified what to avoid

### Review Checklist

- [ ] Matches spec (no more, no less)
- [ ] All tests pass
- [ ] Realtime-safe (if applicable)
- [ ] Thread-safe (if applicable)
- [ ] No unnecessary complexity
- [ ] Readable without excessive comments
- [ ] No magic numbers
- [ ] Error handling complete
