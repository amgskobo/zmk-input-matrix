# ZMK Input Matrix (zip_matrix)

トラックパッドの絶対座標（X/Y）を、設定可能なジェスチャグリッドに変換する ZMK 入力プロセッサです。長押しに対応し、ジェスチャは標準的な KSCAN マトリクスイベントとして報告されるため、ZMK Studio と完全に互換性があります。

## 特徴

- **可変グリッド**: 任意のグリッドサイズ（1×1, 2×2, 3×3 等）を設定可能
- **ブロック配置**: 各ジェスチャ（Tap / Up / Down / Left / Right）が独立したブロックとして垂直に積層
- **SYN ラッチ**: 座標を `INPUT_SYN_REPORT` でラッチし、安定した開始点を確定
- **長押し対応**: Tap ホールド時間を設定可能（0 で無効化）
- **レイヤ変更に対して安全**: 接触の途中でレイヤが変わっても、押下済みのセルは解放され、保留中のホールドはキャンセルされます
- **分割キーボード対応**: セントラル側での処理に最適化
- **スレッドセーフ**: スピンロックにより競合状態を防止

## インストール

ZMK の設定ファイル `config/west.yml` に本プロジェクトを追加してください。

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-matrix
      remote: amgskobo
      revision: main
```

## クイックスタート

### 1. DTS のインクルード

シールドの `.overlay` または `.zmk.dts` で標準ヘッダーをインクルードしてください。

```dts
#include <zmk-input-matrix/input_matrix.dtsi>
```

このヘルパーは標準の `zip_matrix` 入力プロセッサと `kscan_gesture` KSCAN proxy
ノードを定義します。同等のノードを自分で定義する場合だけ、省略できます。

### 2. 設定例 (3x3 グリッド)

この例では **15 行 × 3 列** のマトリクスを作成します（5 ジェスチャブロック × 3 ゾーン）:

**注意**: `CONFIG_ZMK_POINTING` が有効な構成では、DeviceTree で compatible を有効にすると、Kconfig のデフォルト設定により `CONFIG_ZMK_INPUT_PROCESSOR_MATRIX` と `CONFIG_ZMK_KSCAN_INPUT_MATRIX` が自動的に有効になります。

```dts
/* グリッドに合わせて kscan_gesture の rows/columns を設定 */
&kscan_gesture {
    rows = <15>;    /* 5 ジェスチャ × 3 グリッド行 */
    columns = <3>;
};

/* ジェスチャグリッドの構成 */
&zip_matrix {
    rows = <3>;
    columns = <3>;
    flick-threshold = <50>;
    x = <1024>;
    y = <1024>;
    long-press-ms = <300>;
};

/* トラックパッドのパイプラインに zip_matrix を追加 */
&trackpad_listener {
    input-processors = <&zip_matrix>;
};
```

#### ジェスチャ仕様

- 開始座標は `BTN_TOUCH` 押下後、X/Y 座標が両方揃った最初の sync でラッチされます。
- 開始点からの移動量が `flick-threshold` に達した時点で、その接触はフリックとして成立します。移動量は真の距離で測るため、しきい値は開始点を中心とする**正方形ではなく円**を表します。斜め方向のストロークも、まっすぐな場合と同じ長さで成立します。
- 成立後も接触の追跡は続き、**方向は最も遠くまで到達した点**から読み取られます。しきい値を越えた最初のサンプルはストローク全体で最も短いベクトルであり、方向を読むには最も当てになりません。
- フリックが成立した時点で長押しタイマーはキャンセルされ、タッチを離した時点でフリックが press → release として報告されます。
- 長押しホールドは Tap ブロック専用です。`long-press-ms` 以内にフリックが成立しなければ Tap セルが押下され、タッチを離すまで、またはレイヤが変わるまで押下状態を維持します。
- 座標が届かなかった場合、ジェスチャは報告されません。フリックもホールドも成立しなかった場合、タッチを離した時点で Tap が press → release として報告されます。

#### レイヤ変更時の挙動

どのプロセッサが動作するかは、**そのイベント時点で有効なレイヤ**から都度決定されます。したがってレイヤが変わると 1 つの接触が 2 つのチェーンに分割され、このプロセッサは呼ばれなくなり、ジェスチャを終わらせるはずだった `BTN_TOUCH` の release は別のチェーンへ回されます。

そのため、レイヤ変更を直接監視して取り残しが出ないようにしています。

- このインスタンスが既に押下したセルは解放されます。
- 保留中のホールドはキャンセルされます。そうしないと、レイヤが変わった後にタイマーが発火し、誰も解放できないセルを押してしまいます。
- 抑制済みの押下の記録は破棄されます。そうしないと、後から届いた無関係な release が代わりに飲み込まれます。

`suppress-key` は、**ここで抑制していない押下に対応する release は決して破棄しません**。release を通すことは常に安全です（対応する押下は既にホストへ届いている）が、破棄するとそのボタンが押しっぱなしのまま解放手段を失います。

### 3. キーマップ設定

`kscan_gesture` デバイスは 15 行 × 3 列の標準マトリクスとして動作します。各ジェスチャゾーンにキーを割り当てることができます：

```dts
/* .keymapファイル内 */
default_layer {
    bindings = <
        /* 行 0: Tap */
        &kp A &kp B &kp C
        /* 行 1: Tap */
        &kp D &kp E &kp F
        /* 行 2: Tap */
        &kp G &kp H &kp I

        /* 行 3: Up */
        &kp UP &kp UP &kp UP
        /* 行 4: Up */
        &kp UP &kp UP &kp UP
        /* 行 5: Up */
        &kp UP &kp UP &kp UP

        /* 行 6: Down */
        &kp DOWN &kp DOWN &kp DOWN
        /* 行 7: Down */
        &kp DOWN &kp DOWN &kp DOWN
        /* 行 8: Down */
        &kp DOWN &kp DOWN &kp DOWN

        /* 行 9: Left */
        &kp LEFT &kp LEFT &kp LEFT
        /* 行 10: Left */
        &kp LEFT &kp LEFT &kp LEFT
        /* 行 11: Left */
        &kp LEFT &kp LEFT &kp LEFT

        /* 行 12: Right */
        &kp RIGHT &kp RIGHT &kp RIGHT
        /* 行 13: Right */
        &kp RIGHT &kp RIGHT &kp RIGHT
        /* 行 14: Right */
        &kp RIGHT &kp RIGHT &kp RIGHT
    >;
};
```

### 4. 物理レイアウト (ZMK Studio)

ZMK Studio でジェスチャグリッドをブロックごとに分けて表示するには、[keymap-drawer](https://github.com/caksoylar/keymap-drawer) ツールで `keys` 配列を生成します：

```bash
# 間隔付き 15×3 グリッドのキーを生成
python -m keymap_drawer.physical_layout_to_dt --cols-thumbs-notation "333+2 2+333"
```

または [ZMK Physical Layout Converter](https://zmk-physical-layout-converter.streamlit.app/) を使用することもできます。

**ヒント**: 生成後、レイアウトを kscan デバイスに割り当てます：

```dts
&kscan_gesture {
    physical-layout = <&gesture_layout>;
};
```

詳しくは [ZMK Physical Layouts](/docs/development/hardware-integration/physical-layouts) を参照してください。

## 設定リファレンス

| プロパティ | 型 | デフォルト | 説明 |
| :--- | :--- | :--- | :--- |
| `rows` | int | 必須 | グリッドの行数 |
| `columns` | int | 必須 | グリッドの列数 |
| `x` | int | 必須 | 最大 X 座標解像度 |
| `y` | int | 必須 | 最大 Y 座標解像度 |
| `flick-threshold` | int | 必須 | フリック成立に必要な、開始点からの最小移動量 |
| `kscan` | phandle | 必須 | ジェスチャイベントを受け取る `zmk,kscan-input-matrix` プロキシ |
| `long-press-ms` | int | 200 | Tap ホールド時間（ミリ秒）、0 で無効化 |
| `suppress-abs` | bool | false | X/Y だけでなく、すべての `INPUT_EV_ABS` イベントを抑制 |
| `suppress-touch` | bool | false | `INPUT_BTN_TOUCH` のみ抑制 |
| `suppress-key` | bool | false | タッチパッド由来のボタンイベントも含め、すべての `INPUT_EV_KEY` イベントを抑制 |
| `diamond-tap` | bool | false | Tap のみを報告し、1×4 グリッドを D-pad 風のダイヤモンド領域に分割 |

各プロパティの範囲はビルド時に検査されます。`rows`: 1–51、`columns`: 1–255、`x`/`y`: 1–65534、`flick-threshold`: 1–65535、`long-press-ms`: 0–65535。

対応する `kscan_gesture` には同一の `columns` と、`zip_matrix.rows` × 報告されるジェスチャ数（**通常は 5、`diamond-tap` では 1**）の `rows` を設定してください。

### Diamond Tap

`diamond-tap` はタッチ領域を矩形グリッドではなく 2 本の対角線で分割し、指が着地した方向を Tap として報告します。`rows = <1>` と `columns = <4>` が必要です。

| 列 | Tap 領域 |
| :---: | :--- |
| 0 | Up |
| 1 | Right |
| 2 | Down |
| 3 | Left |

対角線上の点は垂直軸が優先されるため、中心ちょうどは Down になります。

**このインスタンスは Tap のみを報告します。** ダイヤモンドは指が「止まった場所」から方向を読み取りますが、これはフリックが測るものと正反対です。小さなパッドでは、ストロークは向かう先の反対側から始めざるを得ないため、2 つの読み取りは逆方向を指します。したがって 4 つのフリック行には決して到達せず、背後の kscan プロキシに必要な行数はグリッド行あたり 5 ではなく 1 になります。

```dts
&kscan_gesture {
    rows = <1>;     /* 1 ジェスチャ（Tap）× 1 グリッド行 */
    columns = <4>;
};

&zip_matrix {
    rows = <1>;
    columns = <4>;
    x = <1024>;
    y = <1024>;
    diamond-tap;
};
```

`flick-threshold` はバインディング上は必須ですが、ダイヤモンドのインスタンスでは効果がありません。

## ライセンス

MIT ライセンス。詳細は [LICENSE](LICENSE) を参照してください。
