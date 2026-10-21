# ZMK Input Matrix (zip_matrix)

[![Test](https://github.com/amgskobo/zmk-input-matrix/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-matrix/actions/workflows/test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[English](README.md)

トラックパッドの絶対座標（X/Y）を、設定可能なジェスチャグリッドに変換する ZMK 入力プロセッサです。長押しに対応し、ジェスチャは標準的な KSCAN マトリクスイベントとして報告されるため、ZMK Studio と完全に互換性があります。

## 互換性

upstream ZMK の `main` と DYA fork の `main+dya` の両方で継続的に
コンパイルテストします。gesture処理本体はどちらでも使用できます。ランタイム設定UIは
任意機能であり、`main+dya` など `zmk-feature-custom-settings` を提供する構成でのみ
有効にします。

## 特徴

- **可変グリッド**: 任意のグリッドサイズ（1×1, 2×2, 3×3 等）を設定可能
- **ブロック配置**: 各ジェスチャ（Tap / Up / Down / Left / Right）が独立したブロックとして垂直に積層
- **SYN ラッチ**: 座標を `INPUT_SYN_REPORT` でラッチし、安定した開始点を確定
- **長押し対応**: Tap ホールド時間を設定可能（0 で無効化）
- **レイヤ変更に対して安全**: 接触の途中でレイヤが変わっても、押下済みのセルは解放され、保留中のホールドはキャンセルされ、接触自体も終了扱いになります
- **分割キーボード対応**: セントラル側での処理に最適化
- **スレッドセーフ**: スピンロックにより競合状態を防止

1つのプロセッサ node を複数の ZMK input listener で共有できます。設定と出力先
KSCAN device は node に属しますが、接触座標、flick 判定、hold work、contact
generation、抑制済みボタン記録は `input_device_index` ごとに独立します。そのため、
local と split 経由の pad が進行中の gesture 状態を上書きすることはなく、左右専用の
matrix processor node を複製する必要もありません。不正な listener index は変換せず
通過し、stream 0 へ alias しません。

遅延 long-press handler が行うのは、上限のあるメモリ内状態更新と KSCAN report だけです。
sleep、動的確保、settings 走査、flash 書き込みは行いません。保持の受け付け中に
接触終了や設定変更が入った場合も、接触世代と解除待ち状態で対応付けます。

## 同じセルを共有する入力

複数の入力元が同じ仮想セルを保持する場合、最初の入力で押下し、最後の
入力が離れたときに解除します。別のプロセッサーノードからの入力も対象です。
保持中のセルへのタップは追加の押下・解除を発生させず、保持を継続します。
異なるセルは独立して処理します。

出力ごとの上限は同時保持16件、待機中・解除予約のイベント32件です。
必要に応じて `CONFIG_ZMK_INPUT_MATRIX_MAX_CLAIMS` と
`CONFIG_ZMK_INPUT_MATRIX_EVENT_QUEUE_SIZE` を調整してください。
上限で新規入力を受け付けられない場合も、受け付け済み入力の解除枠は保持します。
コールバックの停止・交換には、保持状態の再通知や以前の受信側の解除保証はありません。

## 押下と解除の対応

BTN_0〜BTN_15 と BTN_TOUCH は、最初の押下を通したか抑制したかを入力元ごとに
記録します。実行中の設定変更やジェスチャー無効化では、この対応を解除まで保持します。
通過済み押下の繰り返しは、ボタンを再度押さずに同期イベントへ変換し、移動フレームの
終端を保持します。設定変更前の押下が、変更後に解除できなくなることを防ぎます。

レイヤー変更でも入力クリックの押下と解除の対応は保持します。
MatrixのTap判定は、絶対座標、`BTN_TOUCH`、同期イベントから行います。
ドライバーの `BTN_0` タップクリックは使いません。有効中は接触状態を読み取った後に
新しいKEY押下と、その押下をここで抑制した場合の解除を消費します。
BTN_0～BTN_15の解除は、ここで押下を抑制していなければ通します。
通常経路で始まったクリックの予約解除も対象です。BTN_TOUCHは内部接触信号として、
無効中に通した押下の対になる解除を除いて消費します。仮想KSCAN出力は別経路です。
繰り返し押下は無操作の同期イベントになります。

### 必要なレイヤーだけに配置

Matrixは仮想キーを必要とする入力リスナーの経路にだけ置きます。
通常のポインターやスクロールは、それぞれの経路で処理します。
Matrixの入口常設、共有プロセッサー、Matrix本体での二重のレイヤー選択は不要です。

```dts
&zip_matrix { suppress-abs; };

&trackpad_listener {
    /* 既存の通常ポインター処理はこのノードに残します。 */
    gestures {
        layers = <1>;
        input-processors = <&zip_matrix>;
    };
};
```

レイヤー変更イベントは、Matrixへ入力が来なくなっても仮想キーの解除、
長押し予約の取り消し、接触状態の破棄を直接行います。次の入力を待ちません。
Tapは座標・BTN_TOUCH・同期から生成し、ドライバーのBTN_0タップクリックは使いません。
通常経路で押され、Matrixへ移った後に届くBTN_0～BTN_15の解除は、
その押下をこのstreamが抑制していなければ通過します。

押下と解除、フレームの記録は、実際にMatrixへ来たイベントについてだけ有効です。
別経路のクリックや同期終端は観測できません。通常のON/OFFが対になった入力について、
予約解除の完了、マウスボタン・仮想キーの押下が残らないこと、その後のポインター入力の
回復を合格条件とします。経路変更時に別入力元のクリックが早く解除される場合があり、
経路をまたぐ厳密なクリック保持時間と所有関係は保証しません。
Matrixを通らない経路へON→ON→OFFを送ると押下が残る場合があります。
これらの追加保証は[所有者テスト](tests/deep-scan/README.md)の厳格検査に残しており、
失敗を成功扱いにはしていません。無効化中のMatrixへ戻った際、別経路のOFFを観測できて
いなければ最初のONを繰り返しとみなし、そのクリックが欠ける場合があります。
そのON/OFFで記録が同期し、次のクリックは通ります。仮想キーのレイヤー変更時の解除は
別の保証です。

抑制対象の同期付きキー・絶対座標がフレーム終端になった場合、先に通過した未同期入力が
あれば値0の `INPUT_MSC_SCAN` に変換して境界を維持します。すべて抑制されたフレームでは
空のマウス報告を送りません。通過済み押下の繰り返しも同期付きの無操作イベントにします。

## ランタイム設定

`CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS=y` を有効にすると、DYA Studio の
`amgskobo__matrix` に各インスタンスが表示されます。変更できる値は `enabled`、
`flick_threshold`、`long_press_ms`、`suppress_abs` です。永続化は custom-settings registry だけが担当します。
設定更新時は全 listener stream を無効化し、報告済み hold を解放、保留中の work を
キャンセルします。更新と重なった入力イベントは新旧設定を混在させず破棄します。

`rows`、`columns`、`kscan`、`diamond-tap` は virtual matrix と keymap の構造を
決めるため、devicetree 固定です。

設定キーの先頭には devicetree node 名を使用します。Zephyrの永続化名上限に収まる
短いnode名を使用してください。RPCキーと保存時の完全な名前はコンパイル時に検査される
ため、保存できない設定を一度受理して後から失うのではなく、該当する構成のビルドを
明示的に停止します。

### 旧設定からの移行

`suppress_key` と `suppress_btn_touch` はランタイムC構造体とcustom-settings登録から
削除しました。呼び出し側の該当フィールドを削除してください。有効中は新しいKEY押下と
その対になる解除を消費します。ここで押下を抑制していないBTNの解除は通します。
通常ポインターは別の入力リスナー経路で選択します。`enabled` は実行時の
ジェスチャー処理を切り替えます。
削除した設定の保存済み値は読み取りませんが、削除もしません。
Devicetreeの旧 `suppress-key`、`suppress-btn-touch` は移行用の非推奨マーカーとして
受け付けます。省略しても新しいKEY押下の通過は有効になりません。Matrixとクリックを同時に
通していた構成は、更新前に入力経路を変更する必要があります。

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

DYAのランタイム設定を使用する場合は、manifestへ
`zmk-feature-custom-settings` も追加し、central側の設定で連携を有効にします。

```yaml
  remotes:
    - name: dya-source
      url-base: https://github.com/cormoran
  projects:
    - name: zmk-feature-custom-settings
      remote: dya-source
      revision: main
```

```conf
CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS=y
```

devicetreeの固定設定だけで使用する場合、このオプションは不要です。

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

#### マトリクスの対応

| 行の範囲 | ジェスチャ |
|:---------:|:-------:|
| 0 - 2 | Tap |
| 3 - 5 | Up |
| 6 - 8 | Down |
| 9 - 11 | Left |
| 12 - 14 | Right |

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
- 接触自体も終了扱いにします。開始点と一緒に、タッチ状態とバッファ済み座標も破棄します。接触が有効なまま開始点だけ消えた状態は、SYN 処理が「ストロークの開始」と読む形そのものなので、そのままでは開始点を latch し直し、キャンセルしたばかりのホールドを予約し直してしまいます。
- 押下と解除、保留中フレームの記録は、対応する解除と同期境界まで保持します。記録はMatrixが実際に観測したイベントにだけ適用します。

このイベントは**すべてのレイヤ変更**で発火します。このプロセッサがチェーンに残る変更（パッドに指を置いたままキーボード側のレイヤキーに触った場合など）でも同じです。いずれにせよ進行中の接触は破棄され、指を上げて置き直すまでこのインスタンスは反応しません。

有効中は新しいKEY押下を消費します。BTN_0～BTN_15の解除は、ここで押下を抑制していた場合だけ消費します。この記録はMatrixが観測した入力だけに適用します。別経路で失われた片側の押下・解除は修復できません。

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

詳しくは [ZMK Physical Layouts](https://zmk.dev/docs/hardware-integration/physical-layouts) を参照してください。

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

## テスト

```sh
python3 tests/runtime/run.py        # または: bash ./tests/run-docker.sh
bash ./tests/run-integration-docker.sh upstream
bash ./tests/run-integration-docker.sh dya
```

runtime テストは driver の関数そのものを `tests/runtime/` のスタブ付きハーネスに取り出します。
通常実行は全ケースをASan/UBSanで確認し、`ZMK_TEST_MODE=full` では最適化版とgcov版も実行します。geometry ハーネスは grid の端、diamond の領域、
斜め flick の閾値、大きな移動距離と小さなパネルの全座標を確認します。driver ハーネスは
event handler に接触を流し、tap、最遠点を追う flick、長押し、hold を押している間に届く
接触や設定変更、対になるキー抑制、listener ごとの stream、レイヤ変更での解放を再現します。
さらに仮想KSCANプロキシと設定ヘルパーのハーネスがあります。fullモードのCIは取り出した各関数
(座標、driver と layer listener、プロキシ、設定ブリッジ)の行・分岐100%を要求します。
保存された設定をドライバ呼び出しに変える適用処理もマクロごと取り出し、ソースに
ゲートのない関数が1つでもあれば runner が失敗します。devicetree からの
インスタンス生成そのものは下記のファームウェアfixtureで確認します。

各variantで、2つのinput listenerが1つのmatrix nodeを共有するファームウェアfixtureをビルドし、
processorと仮想KSCANプロキシが有効になっていることを確認します。`upstream` はZMK `main` で
ビルドしてcustom settingsが無効のままであることを、`dya` はcustom settingsを有効にしたDYA ZMK
forkでビルドして `amgskobo__matrix` subsystemと全設定キーがファームウェアにリンクされて
いることを確認します。GitHub Actions は pull request、`main` への push、週次実行で両方を確認します。

通常ZMKとDYAの統合検査では、実カーネルの6ケースとMatrixの45ケースを、
それぞれ同じバイナリで3回実行します。両系統のCIは固定したIQS実装を必須とし、
実IQS入力の17ケースを加えた計62ケースを実行します。ホストの通常実行は全ケースをASan/UBSanで確認し、
`ZMK_TEST_MODE=full` では最適化版と全行・全分岐の検査も行います。
準備済み依存環境での実行方法と、実機確認が別途必要な範囲は
[テストガイド](tests/README.md)を参照してください。

## ライセンス

MIT ライセンス。詳細は [LICENSE](LICENSE) を参照してください。
