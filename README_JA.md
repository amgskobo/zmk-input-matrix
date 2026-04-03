# ZMK Input Matrix (zip_matrix)

トラックパッドの絶対座標（X/Y）を、設定可能なジェスチャ・グリッドに変換する ZMK インプット・プロセッサです。長押し（Long-press）に対応し、ジェスチャは標準的な KSCAN マトリックス・イベントとして報告されるため、ZMK Studio との完全な互換性を備えています。

## 特徴

- **ダイナミック・グリッド**: 任意のグリッドサイズ（1x1, 2x2, 3x3 等）を設定可能
- **ブロック配置**: 各ジェスチャ（Tap/Up/Down/Left/Right）が独立したブロックとして垂直にスタック
- **SYNラッチ**: 座標を `INPUT_SYN_REPORT` でラッチし、安定した開始点を確定
- **長押しサポート**: 設定可能なホールド時間（0で無効化可能）
- **分割キーボード対応**: 中央側（Central）での処理に最適化
- **スレッドセーフ**: スピンロックの使用により、競合状態を防止

## インストール

ZMK 設定の `config/west.yml` にこのプロジェクトを追加してください。

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

シールドの `.overlay` または `.zmk.dts` で標準ヘルパーをインクルードしてください。

```dts
#include <zmk-input-matrix/input_matrix.dtsi>
```

### 2. 設定例 (3x3 グリッド)

この例では **15行 × 3列** のマトリックスを作成します（5つのジェスチャブロック × 3つのゾーン）:

**注意**: DeviceTreeでcompatibleを有効にすると、Kconfigのデフォルトにより `CONFIG_ZMK_INPUT_PROCESSOR_MATRIX` と `CONFIG_ZMK_KSCAN_INPUT_MATRIX` の両方が自動的に有効になります。

```dts
/* グリッドに合わせて kscan_gesture の rows/columns を設定 */
&kscan_gesture {
    rows = <15>;    /* 5ジェスチャ * 3グリッド行 */
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

### 3. キーマップ設定

`kscan_gesture`デバイスは15行x3列の標準マトリックスとして動作します。各ジェスチャゾーンにキーを割り当てることができます：

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

### 4. フィジカルレイアウト (ZMK Studio)

ZMK Studio でジェスチャグリッドをブロックごとに分離して表示するには、[keymap-drawer](https://github.com/caksoylar/keymap-drawer) ツールを使って `keys` 配列を生成します：

```bash
# 间隙のある15x3グリッドのキーを生成
python -m keymap_drawer.physical_layout_to_dt --cols-thumbs-notation "333+2 2+333"
```

または [ZMK Physical Layout Converter](https://zmk-physical-layout-converter.streamlit.app/) ウェブツールを使用します。

**ヒント**: 生成後、レイアウトをkscanデバイスに割り当てます：

```dts
&kscan_gesture {
    physical-layout = <&gesture_layout>;
};
```

詳細は [ZMK Physical Layouts](/docs/development/hardware-integration/physical-layouts) を参照してください。

## 設定リファレンス

| プロパティ | 型 | デフォルト | 説明 |
| :--- | :--- | :--- | :--- |
| `rows` | int | 必須 | グリッドの行数 |
| `columns` | int | 必須 | グリッドの列数 |
| `x` | int | 必須 | 最大 X 座標解像度 |
| `y` | int | 必須 | 最大 Y 座標解像度 |
| `flick-threshold` | int | 必須 | フリック判定の最小ピクセル数 |
| `long-press-ms` | int | 300 | ホールド時間（ミリ秒）、0で無効化 |
| `suppress-abs` | bool | false | ABS/ポインターイベントを抑制（抑制時はマウスが動かない） |
| `suppress-key` | bool | false | KEY/タッチイベントを抑制 |

## ライセンス

MIT ライセンス。詳細は [LICENSE](LICENSE) を参照してください。
