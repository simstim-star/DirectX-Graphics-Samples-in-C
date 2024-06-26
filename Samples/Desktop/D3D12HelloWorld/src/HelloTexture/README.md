## DirectX12 Hello Texture in C
This is an adaptation of the official [D3D12HelloWorld HelloTexture](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12HelloWorld/src/HelloTexture) using C.

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

## Result
![image](https://github.com/simstim-star/DirectX-Graphics-Samples-in-C/assets/167698401/77156f18-fbc9-486d-8cf3-5f7f1b5d424d)

