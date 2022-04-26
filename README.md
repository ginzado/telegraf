# inputs.dpdkflow

ポートミラーされたスイッチからパケットを読み込みフロー情報を解析して吐き出す Telegraf プラグイン。

## 特徴

- DPDK を用いることで取りこぼしの少ない高性能な解析。
- MRT ダンプファイルを読み込むことで BGP フルルートがない環境でも AS 解析が可能。

## 使い方

以下、 Telegraf がデータを吐き出す宛先はすでに用意されているものとする。

例としては InfluxDB 2.2 の設定例を示す。

InfluxDB のインストールについては以下のサイトを参照。

https://docs.influxdata.com/influxdb/v2.2/install/?t=Linux#

### ビルド

まず Ubuntu 20.04 LTS の環境を用意。

Telegraf は Go で書かれているのでもし Go の環境の準備がまだなら Go の環境を用意。


```
~$ wget https://go.dev/dl/go1.18.1.linux-amd64.tar.gz
~$ sudo tar -C /usr/local -xzf go1.18.1.linux-amd64.tar.gz
~$ echo "export PATH=$PATH:/usr/local/go/bin" >> ~/.profile
```

上記手順で `~/.profile` を書き換えたら一度ログアウトしてから再度ログイン。

ビルドに必要なパッケージをインストールしソースコードを取得し `make` コマンド実行。
```
~$ sudo apt install build-essential pkgconf dpdk dpdk-dev
~$ git clone https://github.com/ginzado/telegraf.git
~$ cd telegraf
~/telegraf$ git checkout -b dpdkflow origin/dpdkflow
~/telegraf$ make
```

いまいるディレクトリに `telegraf` という実行ファイルができる。

### 設定

以下のような設定ファイルを作成する。

```
[global_tags]
[agent]
  interval = "10s"
  round_interval = true
  metric_batch_size = 1000
  metric_buffer_limit = 10000
  collection_jitter = "0s"
  flush_interval = "10s"
  flush_jitter = "0s"
  precision = "0s"
  hostname = ""
  omit_hostname = false
[[outputs.influxdb_v2]]
  urls = ["http://(InfluxDBのIPアドレス):8086"]
  token = "(InfluxDBにアクセスするための認証トークン)"
  organization = "(InfluxDBの組織名)"
  bucket = "(InfluxDBのバケット名)"
[[inputs.dpdkflow]]
  main_core_index = 1
  interval = 60
  local_nets_ipv4 = ["192.168.1.0/24"]
  local_nets_ipv6 = ["2001:db8:0:1::/64"]
  aggregate_incoming = ["iface", "vlan", "af", "proto", "dst_host", "src_as", "app"]
  aggregate_outgoing = ["iface", "vlan", "af", "proto", "src_host", "dst_as", "app"]
  mrt_rib_path = "path/to/mrt_rib_file"
  [[inputs.dpdkflow.core]]
    index = 2
    [[inputs.dpdkflow.core.port]]
      index = 0
      description = "Port1"
      port_vlan_id = 1
      tag_vlan_ids = [11, 12]
  [[inputs.dpdkflow.core]]
    index = 3
    [[inputs.dpdkflow.core.port]]
      index = 1
      description = "Port2"
      port_vlan_id = 13
      tag_vlan_ids = []
```

`[[outputs.influxdb_v2]]` の部分は実際の InfluxDB の設定に置き換える。

`[[inputs.dpdkflow]]` の部分は大体以下のような意味となる。

|項目名|意味|
|:-----|:---|
|`main_core_index`|収集したデータを `[[outputs.influxdb_v2]]` に吐き出す処理を行う CPU コアの(DPDK 上の)インデックス番号。|
|`interval`|フローを集約する期間。単位は秒。|
|`local_nets_ipv4`|自ネットワークの IPv4 アドレスプレフィクス。|
|`local_nets_ipv6`|自ネットワークの IPv6 アドレスプレフィクス。|
|`aggregate_incoming`|外部ネットワークから自ネットワークへ入ってくるパケットを集約する際にキーとする項目(詳細後述)。|
|`aggregate_outgoing`|自ネットワークから外部ネットワークへ出ていくパケットを集約する際にキーとする項目(詳細後述)。|
|`aggregate_internal`|自ネットワーク間通信のパケットを集約する際にキーとする項目(詳細後述)。|
|`aggregate_external`|外部ネットワーク間通信のパケットを集約する際にキーとする項目(詳細後述)。|
|`mrt_rib_path`|MRT ダンプファイルへのパス。|
|`[[inputs.dpdkflow.core]]`|DPDK でひたすらパケットを拾い続ける CPU コア 1 つ分の定義。例えば 2 つ `[[inputs.dpdkflow.core]]` を定義した場合は 2 コアでパケットを収集する。|
|(`[[inputs.dpdkflow.core]]` の) `index`|CPU コアの(DPDK 上の)インデックス番号。例えば 0 を指定した場合 0 番目の CPU コアで処理が走る。|
|`[[inputs.dpdkflow.core.port]]`|パケットを拾うポート 1 つ分の定義。このポートのパケットはこの定義の親の CPU コアが拾う。 1 つの CPU コアで複数のポートのパケットを拾うことも可能。その時は 1 つの `[[inputs.dpdkflow.core]]` に複数の `[[inputs.dpdkflow.core.port]]` を定義する。|
|(`[[inputs.dpdkflow.core.port]]` の) `index`|ポートの(DPDK 上の)インデックス番号。|
|`description`|ポートの名前。|
|`port_vlan_id`|このポートの VLAN ID。このポートを流れる 802.1Q タグが無いフレームはこの VLAN ID として扱う。|
|`tag_vlan_ids`|このポート上を流れる 802.1Q タグ VLAN ID。ここにない VLAN ID の 802.1Q タグ付きフレームは無視する。|

集約キー(`aggregate_incoming` 等)には以下の項目を指定できる。

|項目名|意味|
|:-----|:---|
|`iface`|フロー情報を拾ったインターフェースの `description`。|
|`af`|アドレスファミリ(`2`(IPv4) か `10`(IPv6))。|
|`proto`|プロトコル(`6`(TCP) や `17`(UDP) や `50`(ESP) や `47`(GRE) など)。|
|`vlan`|VLAN ID。|
|`src_host`|送信元 IP アドレス。|
|`dst_host`|宛先 IP アドレス。|
|`src_as`|送信元 AS 番号。|
|`dst_as`|宛先 AS 番号。|
|`src_port`|送信元ポート番号(TCP か UDP の場合のみ)。|
|`dst_port`|宛先ポート番号(TCP か UDP の場合のみ)。|
|`app`|プロトコル番号とサービス番号(ポート番号)の組。サービス番号は送信元ポート番号と宛先ポート番号の小さい方を採用する。|

### 実行

まず DPDK の実行環境の準備をする。

(以下は root 権限で。)

```
~# mountpoint -q /dev/hugepages || mount -t hugetlbfs nodev /dev/hugepages
~# echo 1024 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
~# modprobe uio_pci_generic
~# dpdk-devbind.py -u 0000:03:00.0
~# dpdk-devbind.py -u 0000:04:00.0
~# dpdk-devbind.py -b uio_pci_generic 0000:03:00.0
~# dpdk-devbind.py -b uio_pci_generic 0000:04:00.0
```
`0000:03:00.0` や `0000:04:00.0` の部分は実際の PCI アドレスに置き換える。実際の PCI アドレスは `sudo dpdk-devbind.py -s` などして確認する。

上記の例では `0000:03:00.0` がポートインデックス番号 0 に、 `0000:04:00.0` がポートインデックス番号 1 になる。

DPDK の準備ができたら作成した設定ファイルを引数に `telegraf` を root 権限で実行する。

```
~$ sudo path/to/telegraf --config path/to/telegraf.conf
```
### 補足

- MRT ダンプファイルは WIDE プロジェクト(Route Views プロジェクト)さんが公開されているこのへん( http://archive.routeviews.org/route-views.wide/bgpdata/2022.04/RIBS/rib.20220425.1200.bz2 )をダウンロードして使わせてもらう(それ以外の MRT ダンプファイルは未検証)。展開してファイル名を適当に変えて `mrt_rib_path` で指定すると起動時に読み込まれる(結構時間がかかる)。最新の MRT ダンプファイルに差し替えたい時は同じファイル名でファイルを差し替えると MRT ダンプファイルのタイムスタンプを見て自動的に更新しようとする。
- 集約項目に `app` がある場合はデータストア(InfluxDB など)に送信されるデータに `app` を表す文字列が格納された `app_desc` という項目も送信される。 `app_desc` は "`tcp(6)/https(443)`" や "`udp(17)/domain(53)`" や "`esp(50)`" のようになる。`/etc/services` にサービス名の登録がないものは "`tcp(6)/unknown(12345)`" のようになる。 `/etc/protocols` にプロトコル名の登録がないものは "`unknown(123)`" のようになる。 `/etc/protocols` と `/etc/services` を書き換えるとそのタイムスタンプから自動的にデータを更新する。
- InfluxDB はめちゃくちゃメモリを食うようなので集約の粒度を細かくする場合は適当にダウンサンプルするようにするかアホみたいにメモリを搭載したマシンで実行する。
