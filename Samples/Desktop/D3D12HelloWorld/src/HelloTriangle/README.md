## DirectX12 Hello Triangle in C
This is an adaptation of the official [D3D12HelloWorld HelloTriangle](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12HelloWorld/src/HelloTriangle) using C.

This was done for educational purposes, in order to learn better about COM and DirectX.

## How to build (MSVC)
Run the following with CMake:

```
cmake -B build-msvc -S . -G "Visual Studio 17 2022"
cmake --build build-msvc
```

This will generate a folder called `build-msvc` with the Visual Studio Solution and the project files.

You can run it to see the iconic "HelloTriangle" below:

![image](https://github.com/simstim-star/DirectX12-HelloTriangle-in-C/assets/167698401/224b0151-8987-448e-988c-113fceb65602)

## How to build (GCC)

You can also build with GCC:

```
cc -mwindows *.c -ldxgi -ld3dcompiler -ld3d12 -ldxguid
```
