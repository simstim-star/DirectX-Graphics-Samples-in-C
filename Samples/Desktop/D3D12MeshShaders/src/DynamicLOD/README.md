## DirectX12 DynamicLOD
This is an adaptation of the official [D3D12MeshShaders DynamicLOD](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12MeshShaders/src/DynamicLOD) using C.

Also check my [DirectX-Graphics-Samples-in-C](https://github.com/simstim-star/DirectX-Graphics-Samples-in-C), where I intend to port all oficial examples to C.

This was done for educational purposes, in order to learn better about COM and DirectX.

## How to build (MSVC)
First, you will need to get [DirectXMath-in-C](https://github.com/simstim-star/DirectXMath-in-C) and install it with:

```
cmake -S . -B build-install
cmake --build build-install --target INSTALL
```

This will generate the folder `xmathc`, probably in `C:/`. This will allow us to use `xmathc` in other projects.

Now we can build this project with:

```
cmake -S . -B build --debug-find-pkg=xmathc --fresh
cmake --build build
```

This will already link `xmathc`.
```
