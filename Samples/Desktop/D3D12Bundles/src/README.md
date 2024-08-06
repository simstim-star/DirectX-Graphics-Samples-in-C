## D3D12Bundles in C
This is an adaptation of the official [D3D12Bundles](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Bundles) using C.

![image](https://github.com/user-attachments/assets/529c48c8-439f-4eb1-9bbd-a79accd42874)


This was done for educational purposes, in order to learn better about COM and DirectX.

Important to note that PIX has no support for C, and for that reason it was removed from this example.

## How to build (MSVC)
First, you will need to get [DirectXMath-in-C](https://github.com/simstim-star/DirectXMath-in-C) and install it with:

```
cmake -S . -B build-install
cmake --build build-install --target INSTALL
```

This will generate the folder `xmathc`, probably in `C:/`. This will allow us to use `xmathc` in other projects.

Now we can build D3D12Bundles:

```
cmake -S . -B build --debug-find-pkg=xmathc --fresh
cmake --build build
```

This will already link `xmathc` and compile the shaders into .cso files.
