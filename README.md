# llvm_custom_passes

Here are some custom LLVM IR passes. It's made with a goal of exploration and experimentation.

## Build

```shell
cmake -S . -B build
cmake --build build
```

## Run

```
opt -load-pass-plugin build/libCustomPasses.dll -passes=RPOPrint,InstrCount -disable-output tests/input.ll
```
