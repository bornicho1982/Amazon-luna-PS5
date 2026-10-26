#!/usr/bin/env python3

path = "apps/luna-ps5/ps5/tooling/app_cpp_runtime.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

content = content.replace("\r\n", "\n")

old_code = """        void *mapping =
            mmap(nullptr, mapped_size, kProtectionReadWrite, kMapPrivateAnonymous, -1, 0);
        if (mapping != reinterpret_cast<void *>(-1))
            header = static_cast<AllocationHeader *>(mapping);
    }
    else
    {
        void *storage = nullptr;
        if (posix_memalign(&storage, kAllocationAlignment, total) == 0)
            header = static_cast<AllocationHeader *>(storage);
    }
    if (!header)
        return nullptr;"""

new_code = """        void *mapping =
            mmap(nullptr, mapped_size, kProtectionReadWrite, kMapPrivateAnonymous, -1, 0);
        if (mapping != reinterpret_cast<void *>(-1))
        {
            header = static_cast<AllocationHeader *>(mapping);
        }
        else
        {
            mapped_size = 0;
            void *storage = nullptr;
            if (posix_memalign(&storage, kAllocationAlignment, total) == 0)
                header = static_cast<AllocationHeader *>(storage);
        }
    }
    else
    {
        void *storage = nullptr;
        if (posix_memalign(&storage, kAllocationAlignment, total) == 0)
            header = static_cast<AllocationHeader *>(storage);
    }
    if (!header)
        return nullptr;"""

if old_code in content:
    content = content.replace(old_code, new_code)
    print("allocate fallback patched!")
else:
    print("allocate target not found!")

old_new = """void *operator new(std::size_t size)
{
    if (void *address = allocate(size))
        return address;
    allocation_failure();
}"""

new_new = """void *operator new(std::size_t size)
{
    if (void *address = allocate(size))
        return address;
    if (void *fallback = malloc(size ? size : 1))
        return fallback;
    allocation_failure();
}"""

if old_new in content:
    content = content.replace(old_new, new_new)
    print("operator new fallback patched!")
else:
    print("operator new target not found!")

with open(path, "w", encoding="utf-8") as f:
    f.write(content)
print("app_cpp_runtime.cpp updated!")
