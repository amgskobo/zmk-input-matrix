# ZMK Input Matrix (zip_matrix)

このモジュールは、トラックパッド（絶対座標 X/Y）を、設定可能な4方向フリック・グリッド・トリガーに変換する ZMK インプット・プロセッサです。

## 特徴

- **ダイナミック・グリッド**: 任意のサイズのグリッド（例：1x1, 2x2, 3x3, 4x3）を設定可能。
- **堅牢なトリガー**: 高精度なサイレンス・ウォッチドッグを使用してジェスチャーの完了を検出し、すべてのトラックパッド・ドライバーとの互換性を確保。
- **柔軟なイベント・フィルタリング**: `suppress-pointer` と `suppress-key` プロパティにより、ABS（ポインター）イベントと KEY（ボタン）イベントを独立して抑制可能。
- **非同期実行**: ZMK の behavior queue を使用してシーケンス化されたバインディング（例：複雑なマクロ）を入力のブロッキングなしに実行。
- **軽量な計算**: 高性能な Q16 固定小数点演算と最大軸比較により、MCU のオーバーヘッドを最小化。
- **1セルあたり5つのジェスチャー**: 中央（タップ）、北、南、西、東。
- **シンプルなタップ判定**: X/Yの両方の入力がなくても、信号が途切れて `timeout-ms` 経過すると中央（タップ）として判定されます。

## インストール

このモジュールを `west.yml` に追加するか、ZMK 設定にコピーしてください。

## 使用方法

### 1. input_matrix.dtsi のインクルード

```dts
#include <input_matrix.dtsi>
```

### 2. オーバーレイの設定

`*.overlay`（または keymap）でプロセッサを定義し、インプット・リスナー（`trackpad_listener` など）に割り当てます。

**注**: Kconfig オプションを手動で有効にする必要はありません。オーバーレイで `zmk,input-processor-matrix` コンパチブル文字列を使用すると、モジュールは自動的に有効になります。

**例: 4x3 グリッド (標準 T9 レイアウト)**

```dts
/* プロセッサの定義 */
&zip_matrix {
    rows = <4>; cols = <3>;
    timeout-ms = <100>;
    flick-threshold = <50>;

    /* 
     * 標準的な 12キー T9 レイアウト (4行 x 3列)
     * [1] [2] [3]
     * [4] [5] [6]
     * [7] [8] [9]
     * [*] [0] [#]
     */

    /* Row 0: 1, 2(ABC), 3(DEF) */
    cell_00 { bindings = <&kp N1 &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
    cell_01 { bindings = <&kp A  &kp C  &kp N2   &kp B    &kp A>; };    /* Tap=A, Left=B, Up=C */
    cell_02 { bindings = <&kp D  &kp F  &kp N3   &kp E    &kp D>; };    /* Tap=D, Left=E, Up=F */

    /* Row 1: 4(GHI), 5(JKL), 6(MNO) */
    cell_03 { bindings = <&kp G  &kp I  &kp N4   &kp H    &kp G>; };
    cell_04 { bindings = <&kp J  &kp L  &kp N5   &kp K    &kp J>; };
    cell_05 { bindings = <&kp M  &kp O  &kp N6   &kp N    &kp M>; };

    /* Row 2: 7(PQRS), 8(TUV), 9(WXYZ) */
    cell_06 { bindings = <&kp P  &kp R  &kp S    &kp Q    &kp N7>; };
    cell_07 { bindings = <&kp T  &kp V  &kp N8   &kp U    &kp T>; };
    cell_08 { bindings = <&kp W  &kp Y  &kp Z    &kp X    &kp N9>; };

    /* Row 3: *, 0, # */
    cell_09 { bindings = <&kp STAR  &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
    cell_10 { bindings = <&kp N0    &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
    cell_11 { bindings = <&kp HASH  &kp UP &kp DOWN &kp LEFT &kp RIGHT>; };
};

/* リスナーへの割り当て */
&trackball_listener {
    input-processors = <&zip_matrix>;
};
```

## 設定リファレンス

### プロセッサ・プロパティ

| プロパティ | 型 | デフォルト | 説明 |
| :--- | :--- | :--- | :--- |
| `rows` | `int` | **必須** | 行数。 |
| `cols` | `int` | **必須** | 列数。 |
| `timeout-ms` | `int` | `100` | 入力が途切れてからジェスチャーを確定するまでの時間 (ms)。 |
| `cooldown-ms` | `int` | `100` | ジェスチャー実行後、次の入力を受け付けるまでのクールダウン時間 (ms)。 |
| `flick-threshold` | `int` | `300` | フリックとタップを区別する最小ピクセル数。 |
| `suppress-pointer` | `bool` | `false` | `true` の場合、ABS イベントの伝播を停止（カーソル移動を無効化）。 |
| `suppress-key` | `bool` | `false` | `true` の場合、KEY イベントの伝播を停止（BTN_TOUCH クリックを無効化）。 |
| `x-min`/`x-max` | `int` | `0`/`1024` | 入力範囲。 |
| `y-min`/`y-max` | `int` | `0`/`1024` | 入力範囲。 |

### 子ノードとグリッドのマッピング

ドライバーは、子ノードを **行優先順**（Row-Major Order: 左から右、上から下）でグリッドセルにマッピングします。

#### マッピングのロジック

1. **ソート**: ドライバーは、子ノードをノード名の **アルファベット順** に処理します。
2. **インデックス**: ノードはインデックス 0 から順にグリッドセルに割り当てられます。
    - `Index = (Row * Total_Cols) + Col`

**重要**: アルファベット順のソートのため、セルが10個以上ある場合は、正しい順序を保つために `cell_01`...`cell_10` のようにゼロ埋めを使用してください。

#### 例: 4x3 グリッド (12セル)

`rows = <4>; cols = <3>;`

| | Column 0 (左) | Column 1 (中央) | Column 2 (右) |
| :--- | :--- | :--- | :--- |
| **Row 0 (上)** | `cell_00` (Idx 0) | `cell_01` (Idx 1) | `cell_02` (Idx 2) |
| **Row 1 (中)** | `cell_03` (Idx 3) | `cell_04` (Idx 4) | `cell_05` (Idx 5) |
| **Row 2 (下)** | `cell_06` (Idx 6) | `cell_07` (Idx 7) | `cell_08` (Idx 8) |
| **Row 3 (底)** | `cell_09` (Idx 9) | `cell_10` (Idx 10) | `cell_11` (Idx 11) |

#### バインディングフォーマット

各子ノードは、以下の特定の順序で正確に5つのエントリを持つ `bindings` 配列を持つ必要があります：

1. **Center** (タップ / フリックなし)
2. **North** (上フリック)
3. **South** (下フリック)
4. **West** (左フリック)
5. **East** (右フリック)

## イベントの透過性と座標系

### 絶対座標 (ABS) と相対座標 (REL) の関係

`zip_matrix` は、ハードウェア・センサーからの **絶対座標 (ABS)** を処理するように設計されています。

- **ハードウェア・レベル**: 多くのタッチパッド等は、絶対的な位置（例：X: 0-1024, Y: 0-1024）を報告します。
- **zip_matrix**: これらの ABS イベントを処理して、どのグリッド・セルがタッチされているかを判断し、「フリック」（ABS 座標での大きな移動）を検出します。
- **標準的なマウス**: ZMK の標準的なマウス動作は、通常、コンバーターや別のインプット・プロセッサを使用して、ABS イベントを **相対的な (REL)** 移動量（デルタ）に変換して報告します。

### イベント・フローと透過性 (Transparency)

ZMK のインプット・プロセッサはチェーン（連鎖）を形成します。`zip_matrix` は、イベントを「消費」するか、チェーンの次のプロセッサへ「透過（パススルー）」させるかを選択できます。

| プロパティ | `true` の場合 (抑制) | `false` の場合 (透過) |
| :--- | :--- | :--- |
| `suppress-pointer` | ABS イベントを消費。カーソルは**動きません**。 | ABS イベントを透過。カーソルが**動きます**。 |
| `suppress-key` | KEY イベント（クリック等）を消費。BTN_TOUCH 等によるクリックを**発生させません**。 | KEY イベントを透過。BTN_TOUCH 等によるクリックが**発生します**。 |

> [!TIP]
> トラックパッドをマクロ・グリッド（T9 キーパッドなど）としてのみ使用する場合は、`suppress-pointer` と `suppress-key` の両方を `true` に設定します。
> マウスとして使いつつジェスチャー機能も持たせたい場合は、これらを `false` に設定し、`zip_matrix` を `input-processors` リストの中で **ABS から REL への変換が行われる前** に配置してください。
