## DirectX12 HelloVAEncode in C
This is an adaptation of the official [D3D12HelloWorld HelloVAEncode](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12HelloWorld/src/HelloVAEncode) using C.

This was done for educational purposes, in order to learn better about COM, DirectX and DXVA.

## How to build (MSVC)
Run the following with CMake:

```
cmake -B build-msvc -S . -G "Visual Studio 17 2022"
cmake --build build-msvc
```

This will generate a folder called `build-msvc` with the Visual Studio Solution and the project files. You may need to reinstall the Microsoft.Direct3D.VideoAccelerationCompatibilityPack with NuGet once you open the project if your solution can't identify the va headers. Also check https://devblogs.microsoft.com/directx/video-acceleration-api-va-api-now-available-on-windows/ for more information on setting up this package.

## How to build (GCC)

You can also build with GCC. You can get the files in https://nuget.org/packages/Microsoft.Direct3D.D3D12/1.613.1 and https://nuget.org/packages/Microsoft.Direct3D.VideoAccelerationCompatibilityPack/1.0.0 with NuGet.

```
cc -mwindows *.c -ldxgi -ld3dcompiler -ldxguid -ld3d12 -lva -lva_win32 \
-L<path/to/Microsoft.Direct3D.D3D12.1.613.1/build/native/bin/x64> \
-I<path/to/Microsoft.Direct3D.D3D12.1.613.1/build/native/include> \
-L<path/to/Microsoft.Direct3D.VideoAccelerationCompatibilityPack.1.0.0/build/native/x64/lib> \
-I<path/to/Microsoft.Direct3D.VideoAccelerationCompatibilityPack.1.0.0/build/native/x64/include>
```
