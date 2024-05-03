## DirectX12 HelloBundles in C
This is an adaptation of the official [D3D12HelloWorld HelloBundles](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12HelloWorld/src/HelloBundles) using C.

This was done for educational purposes, in order to learn better about COM and DirectX.

## How to build (MSVC)
Run the following with CMake:

```
cmake -B build-msvc -S . -G "Visual Studio 17 2022"
cmake --build build-msvc
```

This will generate a folder called `build-msvc` with the Visual Studio Solution and the project files.

## How to build (GCC)

You can also build with GCC:

```
cc -mwindows *.c -ldxgi -ld3dcompiler -ld3d12 -ldxguid
```
