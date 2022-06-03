package lxd

import (
	"bytes"
	"encoding/json"
	"os/exec"
	"time"

	"github.com/influxdata/telegraf"
	"github.com/influxdata/telegraf/config"
	"github.com/influxdata/telegraf/internal"
	"github.com/influxdata/telegraf/plugins/inputs"
)

type Lxd struct {
	Timeout config.Duration `toml:"timeout"`
}

type Cpu struct {
	Usage uint64 `json:"usage"`
}

type Root struct {
	Usage uint64 `json:"usage"`
}

type Disk struct {
	Root Root `json:"root"`
}

type Memory struct {
	SwapUsage     uint64 `json:"swap_usage"`
	SwapUsagePeak uint64 `json:"swap_usage_peak"`
	Usage         uint64 `json:"usage"`
	UsagePeak     uint64 `json:"usage_peak"`
}

type Counters struct {
	BytesReceived          uint64 `json:"bytes_received"`
	BytesSent              uint64 `json:"bytes_sent"`
	ErrorsReceived         uint64 `json:"errors_received"`
	ErrorsSent             uint64 `json:"errors_sent"`
	PacketsDroppedInbound  uint64 `json:"packets_dropped_inbound"`
	PacketsDroppedOutbound uint64 `json:"packets_dropped_outbound"`
	PacketsReceived        uint64 `json:"packets_received"`
	PacketsSent            uint64 `json:"packets_sent"`
}

type Eth0 struct {
	Counters Counters `json:"counters"`
}

type Network struct {
	Eth0 Eth0 `json:"eth0"`
}

type State struct {
	Cpu       Cpu     `json:"cpu"`
	Disk      Disk    `json:"disk"`
	Memory    Memory  `json:"memory"`
	Network   Network `json:"network"`
	Processes uint64  `json:"processes"`
}

type Container struct {
	Name  string `json:"name"`
	State State  `json:"state"`
}

const measurement = "lxd"

var defaultTimeout = config.Duration(time.Second)

func (l *Lxd) Init() error {
	_, err := exec.LookPath("lxc")
	if err != nil {
		return err
	}

	return nil
}

func (l *Lxd) gatherContainer(ct *Container, acc telegraf.Accumulator) {
	tags := map[string]string{
		"name": ct.Name,
	}
	fields := map[string]interface{}{
		"cpu_usage":                        ct.State.Cpu.Usage,
		"disk_usage":                       ct.State.Disk.Root.Usage,
		"memory_usage":                     ct.State.Memory.Usage,
		"swap_usage":                       ct.State.Memory.SwapUsage,
		"network_bytes_received":           ct.State.Network.Eth0.Counters.BytesReceived,
		"network_bytes_sent":               ct.State.Network.Eth0.Counters.BytesSent,
		"network_errors_received":          ct.State.Network.Eth0.Counters.ErrorsReceived,
		"network_errors_sent":              ct.State.Network.Eth0.Counters.ErrorsSent,
		"network_packets_dropped_inbound":  ct.State.Network.Eth0.Counters.PacketsDroppedInbound,
		"network_packets_dropped_outbound": ct.State.Network.Eth0.Counters.PacketsDroppedOutbound,
		"network_packets_received":         ct.State.Network.Eth0.Counters.PacketsReceived,
		"network_packets_sent":             ct.State.Network.Eth0.Counters.PacketsSent,
		"processes":                        ct.State.Processes,
	}
	acc.AddCounter(measurement, fields, tags)
}

func (l *Lxd) Gather(acc telegraf.Accumulator) error {
	lxcPath, err := exec.LookPath("lxc")
	if err != nil {
		return err
	}

	cmdName := lxcPath
	cmd := exec.Command(cmdName, "list", "--format", "json")

	var out bytes.Buffer
	cmd.Stdout = &out
	err = internal.RunTimeout(cmd, time.Duration(l.Timeout))
	if err != nil {
		return err
	}

	bytes := out.Bytes()

	var cts []Container
	err = json.Unmarshal(bytes, &cts)
	if err != nil {
		return err
	}

	for _, ct := range cts {
		l.gatherContainer(&ct, acc)
	}

	return nil
}

func init() {
	inputs.Add("lxd", func() telegraf.Input {
		return &Lxd{
			Timeout: defaultTimeout,
		}
	})
}
