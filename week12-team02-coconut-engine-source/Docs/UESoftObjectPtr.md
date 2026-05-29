FSoftObjectPtr (Non-templated base)
└── TPersistentObjectPtr<FSoftObjectPtr> (Intermediate logic)
    └── TSoftObjectPtr<T> (The templated class you use)
```

The base class, **`FSoftObjectPtr`**, contains the actual `FSoftObjectPath` and the logic to resolve it into a `UObject*`. Crucially, its `Get()` method returns a generic **`UObject*`** rather than a specific `T*`.

### 2. How `FSoftObjectProperty` Performs the Access
When you call `GetObjectPropertyValue(void* Data)`, the engine doesn't care about the `<T>`. It knows that any `TSoftObjectPtr<T>` has the **exact same memory layout** as its base class `FSoftObjectPtr`.

The implementation works roughly like this:

```cpp
// Inside FSoftObjectProperty (Simplified Engine Logic)
UObject* FSoftObjectProperty::GetObjectPropertyValue(const void* PropertyValueAddress) const
{
    // 1. Interpret the raw memory address as the non-templated base class
    const FSoftObjectPtr* SoftPtr = (const FSoftObjectPtr*)PropertyValueAddress;

    // 2. Call the base class's Get() method
    // This method returns a UObject* and doesn't need to know the template type <T>
    return SoftPtr->Get();
}
```

### 3. Binary Compatibility
This works because a `TSoftObjectPtr<T>` is what is called a **"Transparent Wrapper."** It doesn't add any new member variables to the base class. It only adds templated helper functions (like `Cast<T>`) that exist solely for the convenience of the C++ programmer. 

At a memory level:
*   `sizeof(FSoftObjectPtr)` == 32 bytes (approx).
*   `sizeof(TSoftObjectPtr<AActor>)` == 32 bytes (approx).

Because the memory layout is identical, the reflection system can safely treat the memory at `PropertyValueAddress` as if it were a simple `FSoftObjectPtr`.

### 4. How it maintains Type Safety
You might wonder: *"If it's all generic UObject*, what stops me from putting a Texture into a Soft Mesh property?"*

The safety happens at the **`FProperty`** level, not the data level. 
Every `FSoftObjectProperty` instance has a member variable called **`PropertyClass`**. When the Editor or a Blueprint tries to set a value, the `FSoftObjectProperty` checks if the new object's class is a child of its stored `PropertyClass`. 

*   **The Data (`TSoftObjectPtr`):** Is "Dumb" and treats everything as a `UObject*`.
*   **The Metadata (`FSoftObjectProperty`):** Is "Smart" and knows that this specific property should only accept `UStaticMesh`.

### Summary
`FSoftObjectProperty` works by **casting the raw memory address** to a non-templated base class (**`FSoftObjectPtr`**). Since the templated version adds no data, the base class can successfully extract the path and the cached pointer as a generic `UObject*` without ever needing to know what the `<T>` was.