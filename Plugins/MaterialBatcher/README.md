# MaterialBatcher 插件

选中一个 Static Mesh Actor，把它身上**共享同一母材质**的多个材质实例合批成
**一个材质实例 + 一张图集贴图**，并重映射 UV，使渲染结果尽量保持不变。
相当于把"多材质槽物体"转换成"单材质槽物体"，用于减少 Draw Call / 合批。

## 使用方式

1. 关卡视口中选中一个 Static Mesh Actor；
2. 右键 → `Material Batcher` → `合批材质与贴图`；
3. 弹窗提示结果。新资产生成在**源 Mesh 同目录下的 `Batched/` 子文件夹**：
   - `<Mesh>_Batched`         合批后的新 StaticMesh（单材质槽）
   - `<Mesh>_Batched_MIC`     新材质实例
   - `<Mesh>_Atlas_<参数名>`  每个纹理参数一张图集

## 工作原理

1. 校验所有材质槽都是 `UMaterialInstance` 且共享同一 `UMaterial` 母材质；
2. 枚举母材质的**纹理参数**，逐槽取出各自绑定的贴图；
3. 按 `ceil(sqrt(N))` 计算 NxN 图集网格，slot i 放在 cell `(i%N, i/N)`，
   每个 cell 默认 1024px，缩放贴图写入（保留 alpha）；
4. 新建材质实例绑定图集贴图，并复制标量/向量参数；
5. 复制 Mesh，把每个面的 UV0 按其原材质槽缩放平移到对应 cell，
   再把所有 polygon group 合并为一个，设为单一材质槽；
6. 选中的 Actor 指向新 Mesh + 新材质。

## 重要限制（务必阅读）

- **UV 必须在 [0,1] 内**：一旦原 UV 有平铺(tiling)超出 0~1，图集里旁边就是别的贴图，
  会串色。这是图集合并的固有限制（UE 自带 Merge Actors 同理）。
- **仅处理编辑器源格式为 `TSF_BGRA8` 的贴图**：其它格式的槽会被填成透明，
  需要先在源贴图上确认格式（大多数导入的 png/tga 满足）。
- **各材质实例的标量/向量参数应一致**：不一致时取第一个槽的值，并在弹窗给出警告。
- 只处理 LOD0；不改动原始资产，全部生成新资产（可安全撤销 Actor 的 Mesh 指向）。

## 构建说明

- 工程 target 标注引擎版本为 **UE 5.8**（`EngineIncludeOrderVersion.Unreal5_8`）。
- 本机情况：`F:/UnrealEngine-dev-5.8` 的引擎**源码树不完整**
  （`Engine/Source/Programs/Shared` 缺少 `UnrealEngine.csproj.props` 等文件，
  且无预编译的 UnrealBuildTool / UnrealEditor），因此 UBT 无法自举，本插件**尚未在本机实机编译**。
- 代码中用到的所有引擎 API 均已对照 5.8 引擎头文件核对签名（见下）。
- 编译方式：补全 5.8 引擎源码（或换用完整安装的 5.8），然后在
  Rider / VS 里重新生成工程文件并编译 `Test58Editor`，
  或命令行：
  ```
  <Engine>/Engine/Build/BatchFiles/Build.bat Test58Editor Win64 Development -Project="F:/UnrealProject/Test58/Test58.uproject" -WaitMutex
  ```

### 已核对的 5.8 API
- `FImageUtils::ImageResize(..., bResizeSRGBinLinearSpace, bForceOpaqueOutput=false)`
- `FTextureSource::Init(W,H,slices,mips,TSF_BGRA8, uint8*)` / `GetMipData(TArray64<uint8>&, mip)`
- `UMaterialInterface::GetBaseMaterial() / GetAll*ParameterInfo() / GetTextureParameterValue()`
- `UMaterialInstanceConstant::Set{Texture,Scalar,Vector}ParameterValueEditorOnly()`
- `FStaticMeshAttributes::GetVertexInstanceUVs() / GetPolygonGroupMaterialSlotNames()`（= ImportedMaterialSlotName）
- `UStaticMesh::GetMeshDescription/CommitMeshDescription/Get(Set)StaticMaterials`
- `FMeshDescription::SetPolygonPolygonGroup/DeletePolygonGroup/GetPolygonGroupPolygonIDs/GetPolygonVertexInstances`
- `IAssetTools::CreateAsset(Name, Path, Class, Factory)`
