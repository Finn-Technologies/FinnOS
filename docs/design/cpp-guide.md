# C++ in this tree

You do not need advanced C++ to work here. The new tree uses a small, plain
subset on purpose: classes, `std::string`, `std::vector`, `std::optional`,
`std::string_view`, `std::unique_ptr`. No template metaprogramming, no
operator overloading, no multiple inheritance, no exceptions across module
boundaries.

## Toolchain

C++20, `g++` or `clang++`. The flags the tree uses:

```
-std=c++20 -Wall -Wextra -Wpedantic -Werror
```

Tests also build with `-fsanitize=address,undefined`. If the sanitizer finds
something, it is a bug even when the program "works".

## The types you will actually use

```cpp
std::string        // owns text
std::string_view   // borrows text; never outlives the string it points at
std::vector<T>     // owns a growable list
std::optional<T>   // "a T, or nothing" — better than a magic value
std::unique_ptr<T> // owns one object; deletes it automatically
std::span<T>       // a view over an array; does not own it
```

`std::optional` replaces `NULL` and `-1` returns:

```cpp
std::optional<App> find(std::string_view id);
if (auto app = registry.find("files")) {
    draw(*app);
}
```

## Ownership rules

1. **Never call `new` or `delete`.** Use `std::make_unique` or a value.
2. **Never store an owning raw pointer.** A raw pointer or reference is a
   borrow, and the comment says who owns the target.
3. **One owner per file descriptor, socket and Wayland resource.** Wrap it in
   a small RAII type with a destructor that closes it; the C API stays at the
   edge.
4. **A function that fails returns `std::optional` or a `Result`.** Do not use
   an out-parameter and a `bool`.
5. **Exceptions only during startup.** A missing catalogue is a value, not a
   throw. Do not throw across a module boundary.

## Naming

- Files: `snake_case.hpp`, `snake_case.cpp`.
- Types: `PascalCase`.
- Functions, variables, members: `snake_case`.
- Private members end with `_`.
- Constants: `kPascalCase`.
- Namespace: `finn`.

One class per file pair, named after the class.

## Adding a module

1. Create `src/<module>/<name>.hpp` with the interface and a short comment
   saying what it owns.
2. Create `src/<module>/<name>.cpp` with the implementation.
3. Add a test under `tests/unit/` and a target in its `Makefile`.
4. Run `make test-unit`.
5. If it needs a constant another module also needs, put that constant in
   `src/common`, not in both.

## Hands-on tasks

These are small and self-contained. Pick one and say which you are taking.

- [ ] `AppRegistry::load` currently keeps the first duplicate id silently.
      Decide whether the catalogue should reject the file instead, write the
      test, and make it so.
- [ ] Extract the `APP.LIST` reply formatting from `src/session/main.cpp` into
      a pure function and test it without a socket.
- [ ] Add `finn-session --registry` that prints the whole catalogue with its
      installed flag, and a test for its output.
- [ ] Add a `Canvas` unit test for button press/release state using a fake
      `Input`; it needs no display and no Wayland connection.
- [ ] Add an input-injection test on the VM: uinput pointer and key events
      reach the focused toplevel and the shell menu opens and closes.
