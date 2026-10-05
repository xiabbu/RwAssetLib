# RwAssetLib

## English:

A C++17 library for reading, writing, and generating RenderWare 3.7 assets, with Dragon Ball Online (DBO) NTL extensions.

Supports DFF models, ANM skeletal animations, and UVA UV animation dictionaries.

Reads and writes RenderWare extensions including Toon, MatFX, Skin, HAnim, BinMesh, UVAnim, LtMap, UserData, Morph, and Collis, along with DBO NTL material, world material, and Atomic extensions.

Provides generation routines for triangle strips, BinMesh, Toon geometry data, collision trees, and bounding spheres, plus reconstruction of skinned vertex positions and normals from bone poses.

### Project structure

```text
src/
├── core/     # RW types, binary streams, chunks, and diagnostics
├── world/    # Clump, Geometry, Material, Texture references, Camera, and Light
├── plugin/   # RenderWare extension readers and writers
├── ntl/      # DBO NTL material and Atomic extensions
├── anim/     # ANM skeletal animations and UVA UV animation dictionaries
├── dff/      # DFF document loading and saving
├── build/    # Geometry generation and skinning routines
└── tools/    # rwasset CLI: inspection, validation, regeneration, and comparison
```

### Build

Requires CMake 3.16+ and C++17. Windows x64 / Visual Studio 2019:

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release --parallel
```

Outputs: `build/lib/Release/RwAssetLib.lib`, `build/bin/Release/rwasset.exe`.

### CLI

```powershell
.\build\bin\Release\rwasset.exe info model.dff
.\build\bin\Release\rwasset.exe validate model.dff --derive-sizes
```

Run `rwasset` without arguments to list commands.

## 中文:

RenderWare 3.7 的 C++17 资产读写与生成库，包含 Dragon Ball Online（DBO）的 NTL 扩展。

当前支持 DFF 模型、ANM 骨骼动画和 UVA UV 动画字典。

支持 Toon、MatFX、Skin、HAnim、BinMesh、UVAnim、LtMap、UserData、Morph 和 Collis 等 RenderWare 扩展的读写，以及 DBO NTL 材质、世界材质和 Atomic 扩展。

提供三角条带、BinMesh、Toon 几何数据、碰撞树和包围球的生成算法，并支持根据骨骼姿态重建蒙皮顶点位置与法线。

### 项目结构

```text
src/
├── core/     # RW 类型、二进制流、Chunk 和诊断
├── world/    # Clump、Geometry、Material、Texture 引用、Camera 和 Light
├── plugin/   # RenderWare 扩展的读写
├── ntl/      # DBO NTL 材质与 Atomic 扩展
├── anim/     # ANM 骨骼动画与 UVA UV 动画字典
├── dff/      # DFF 文档加载与保存
├── build/    # 几何数据生成与蒙皮计算
└── tools/    # rwasset 命令行工具：结构查看、验证、重建与比较
```

### 构建

需要 CMake 3.16+ 和 C++17。Windows x64 / Visual Studio 2019 构建：

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release --parallel
```

产物：`build/lib/Release/RwAssetLib.lib`、`build/bin/Release/rwasset.exe`。

### 命令行

```powershell
.\build\bin\Release\rwasset.exe info model.dff
.\build\bin\Release\rwasset.exe validate model.dff --derive-sizes
```

不带参数运行 `rwasset` 可查看命令列表。
