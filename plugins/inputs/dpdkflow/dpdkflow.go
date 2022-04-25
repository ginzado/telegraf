package dpdkflow

// #cgo pkg-config: libdpdk
// #include <rte_config.h>
// #include <rte_eal.h>
// #include <rte_ethdev.h>
// #include <rte_cycles.h>
// #include <rte_lcore.h>
// #include <rte_mbuf.h>
// #include <rte_rwlock.h>
// #include "dpdkflow_cgo.h"
import "C"

import (
	"fmt"
	"net"
	"time"
	"unsafe"

	"github.com/influxdata/telegraf"
	"github.com/influxdata/telegraf/plugins/inputs"
)

type DpdkFlowPort struct {
	Index       int    `toml:"index"`
	Description string `toml:"description"`
	PortVlanId  int    `toml:"port_vlan_id"`
	TagVlanIds  []int  `toml:"tag_vlan_ids"`
}

type DpdkFlowCore struct {
	Index int            `toml:"index"`
	Ports []DpdkFlowPort `toml:"port"`
}

type DpdkFlow struct {
	MainCoreIndex     int            `toml:"main_core_index"`
	Interval          int            `toml:"interval"`
	LocalNetsIpv4     []string       `toml:"local_nets_ipv4"`
	LocalNetsIpv6     []string       `toml:"local_nets_ipv6"`
	AggregateIncoming []string       `toml:"aggregate_incoming"`
	AggregateOutgoing []string       `toml:"aggregate_outgoing"`
	AggregateInternal []string       `toml:"aggregate_internal"`
	AggregateExternal []string       `toml:"aggregate_external"`
	MrtRibPath        string         `toml:"mrt_rib_path"`
	Cores             []DpdkFlowCore `toml:"core"`

	acc telegraf.Accumulator
	ctx *C.struct_dpdkflow_context
}

var globalDf *DpdkFlow

func NewDpdkFlow() *DpdkFlow {
	df := &DpdkFlow{}
	if globalDf == nil {
		globalDf = df
	}
	return df
}

func ifaceStr(iface int8) string {
	if globalDf != nil {
		for _, c := range globalDf.Cores {
			for _, p := range c.Ports {
				if p.Index == int(iface) {
					return p.Description
				}
			}
		}
	}
	return "unknown"
}

func directionStr(direction int8) string {
	switch direction {
	case int8(C.direction_incoming):
		return "incoming"
	case int8(C.direction_outgoing):
		return "outgoing"
	case int8(C.direction_internal):
		return "internal"
	case int8(C.direction_external):
		return "external"
	}
	return "unknown"
}

func afStr(af uint8) string {
	switch af {
	case uint8(C.af_ipv4):
		return "ipv4"
	case uint8(C.af_ipv6):
		return "ipv6"
	}
	return "unknown"
}

func hostStr(host unsafe.Pointer) string {
	var hostStr string
	ba := (*[16]byte)(host)
	if ba[0] == 0 && ba[1] == 0 && ba[2] == 0 && ba[3] == 0 &&
		ba[4] == 0 && ba[5] == 0 && ba[6] == 0 && ba[7] == 0 &&
		ba[8] == 0 && ba[9] == 0 && ba[10] == 0 && ba[11] == 0 {
		/* IPv4 */
		hostStr = net.IPv4(ba[12], ba[13], ba[14], ba[15]).String()
	} else {
		/* IPv6 */
		hostStr = net.IP{
			ba[0], ba[1], ba[2], ba[3],
			ba[4], ba[5], ba[6], ba[7],
			ba[8], ba[9], ba[10], ba[11],
			ba[12], ba[13], ba[14], ba[15],
		}.String()
	}
	return hostStr
}

func aggregateFlags(aggregate []string) (uint32, error) {
	var flags uint32
	for _, aggr := range aggregate {
		switch aggr {
		case "iface":
			flags |= uint32(C.aggregate_f_iface)
		case "af":
			flags |= uint32(C.aggregate_f_af)
		case "proto":
			flags |= uint32(C.aggregate_f_proto)
		case "vlan":
			flags |= uint32(C.aggregate_f_vlan)
		case "src_host":
			flags |= uint32(C.aggregate_f_src_host)
		case "dst_host":
			flags |= uint32(C.aggregate_f_dst_host)
		case "src_as":
			flags |= uint32(C.aggregate_f_src_as)
		case "dst_as":
			flags |= uint32(C.aggregate_f_dst_as)
		case "src_port":
			flags |= uint32(C.aggregate_f_src_port)
		case "dst_port":
			flags |= uint32(C.aggregate_f_dst_port)
		case "app":
			flags |= uint32(C.aggregate_f_app)
		default:
			return 0, fmt.Errorf("aggregateFlags: unknown aggregate: %s", aggr)
		}
	}
	return flags, nil
}

//export gather
func gather(d *C.struct_dpdkflow_metric) int {
	tags := map[string]string{
		"iface":     ifaceStr(int8(d.iface)),
		"direction": directionStr(int8(d.direction)),
		"af":        afStr(uint8(d.af)),
		"proto":     fmt.Sprint(uint8(d.proto)),
		"vlan":      fmt.Sprint(int32(d.vlan)),
		"src_host":  hostStr(unsafe.Pointer(&d.src_host[0])),
		"dst_host":  hostStr(unsafe.Pointer(&d.dst_host[0])),
		"src_as":    fmt.Sprint(int64(d.src_as)),
		"dst_as":    fmt.Sprint(int64(d.dst_as)),
		"src_port":  fmt.Sprint(int(d.src_port)),
		"dst_port":  fmt.Sprint(int(d.dst_port)),
		"app":       fmt.Sprintf("%08x", uint32(d.app)),
		"app_desc":  C.GoString(&d.app_desc[0]),
	}
	fields := map[string]interface{}{
		"packets": uint64(d.packets),
		"bytes":   uint64(d.bytes),
	}
	if globalDf != nil {
		globalDf.acc.AddGauge("dpdkflow", fields, tags, time.Now())
	}
	return 0
}

const sampleConfig = `
  ##
  # main_core_index = 2
  ##
  # interval = 300
  ##
  # local_nets_ipv4 = ["192.168.1.0/24", 172.16.1.0/24]
  ##
  # local_nets_ipv6 = ["2001:db8:1::/48"]
  ##
  # aggregate_incoming = ["iface", "vlan", "direction", "af", "proto", "app", "src_as", "dst_host"]
  ##
  # aggregate_outgoing = ["iface", "vlan", "direction", "af", "proto", "app", "dst_as", "src_host"]
  ##
  # aggregate_internal = ["iface", "vlan", "direction", "af", "proto", "app", "dst_host", "src_host"]
  ##
  # aggregate_external = ["iface", "vlan", "direction", "af", "proto", "app", "dst_as", "src_as"]
  ##
  # mrt_rib_path = "/opt/dpdkflow/db/mrt_rib"
  ##
  [[inputs.dpdkflow.core]]
    ##
    # index = 3
    ##
    [[inputs.dpdkflow.core.port]]
      ##
      # index = 0
      ##
      # description = "Port Description"
      ##
      # port_vlan_id = 1
      ##
      # tag_vlan_ids = [2, 3]
`

func (df *DpdkFlow) SampleConfig() string {
	return sampleConfig
}

func (df *DpdkFlow) Description() string {
	return "DPDK-based flow collector"
}

func (df *DpdkFlow) Init() error {
	if df != globalDf {
		return fmt.Errorf("dpdkflow cannot be started more than once")
	}
	if df.Interval == 0 {
		df.Interval = 300
	}
	if len(df.MrtRibPath) > 255 {
		return fmt.Errorf("mrt_rib_path too long")
	}
	if len(df.Cores) > int(C.core_max) {
		return fmt.Errorf("core too many")
	}
	for _, c := range df.Cores {
		if len(c.Ports) > int(C.port_max) {
			return fmt.Errorf("core", c.Index, "port too many")
		}
		for _, p := range c.Ports {
			if len(p.TagVlanIds) > int(C.vlan_max) {
				return fmt.Errorf("core", c.Index, "port", p.Index, "vlan too many")
			}
		}
	}
	coresMap := make(map[int]bool)
	for _, c := range df.Cores {
		if _, ok := coresMap[c.Index]; ok {
			return fmt.Errorf("core index %d dup", c.Index)
		}
		coresMap[c.Index] = true
	}
	if _, ok := coresMap[df.MainCoreIndex]; ok {
		return fmt.Errorf("core index %d dup", df.MainCoreIndex)
	}
	portsMap := make(map[int]bool)
	for _, c := range df.Cores {
		for _, p := range c.Ports {
			if _, ok := portsMap[p.Index]; ok {
				return fmt.Errorf("port index %d dup", p.Index)
			}
			portsMap[p.Index] = true
		}
	}
	var err error
	_, err = aggregateFlags(df.AggregateIncoming)
	if err != nil {
		return fmt.Errorf("aggregate_incoming error: %v\n", err)
	}
	_, err = aggregateFlags(df.AggregateOutgoing)
	if err != nil {
		return fmt.Errorf("aggregate_outgoing error: %v\n", err)
	}
	_, err = aggregateFlags(df.AggregateInternal)
	if err != nil {
		return fmt.Errorf("aggregate_internal error: %v\n", err)
	}
	_, err = aggregateFlags(df.AggregateExternal)
	if err != nil {
		return fmt.Errorf("aggregate_external error: %v\n", err)
	}
	return nil
}

func (df *DpdkFlow) Gather(_ telegraf.Accumulator) error {
	return nil
}

func (df *DpdkFlow) Start(acc telegraf.Accumulator) error {
	fmt.Println("DpdkFlow.Start()")
	fmt.Println("MainCoreIndex: ", df.MainCoreIndex)
	fmt.Println("Interval: ", df.Interval)
	fmt.Println("LocalNetsIpv4: ", df.LocalNetsIpv4)
	fmt.Println("LocalNetsIpv6: ", df.LocalNetsIpv6)
	fmt.Println("AggregateIncoming: ", df.AggregateIncoming)
	fmt.Println("AggregateOutgoing: ", df.AggregateOutgoing)
	fmt.Println("AggregateInternal: ", df.AggregateInternal)
	fmt.Println("AggregateExternal: ", df.AggregateExternal)
	fmt.Println("MrtRibPath: ", df.MrtRibPath)
	for i, c := range df.Cores {
		fmt.Println("Core", i, ":", c.Index)
		for j, p := range c.Ports {
			fmt.Println("Port", j, ":", p.Index)
			fmt.Println("Description: ", p.Description)
			fmt.Println("PortVlanId: ", p.PortVlanId)
			fmt.Println("TagVlanIds: ", p.TagVlanIds)
		}
	}

	df.acc = acc

	df.ctx = &C.struct_dpdkflow_context{
		done:            0,
		running:         0,
		main_core_index: C.int(df.MainCoreIndex),
		interval:        C.int(df.Interval),
	}

	local_nets_ipv4_index := 0
	for _, n := range df.LocalNetsIpv4 {
		_, ipnet, err := net.ParseCIDR(n)
		if err != nil {
			continue
		}
		for i := 0; i < 16; i += 1 {
			df.ctx.local_nets_ipv4_pfix[local_nets_ipv4_index][i] = 0
		}
		for i := 0; i < 4; i += 1 {
			df.ctx.local_nets_ipv4_pfix[local_nets_ipv4_index][12+i] = C.uint8_t(ipnet.IP[i])
		}
		ones, _ := ipnet.Mask.Size()
		df.ctx.local_nets_ipv4_plen[local_nets_ipv4_index] = C.uint8_t(ones)
		local_nets_ipv4_index += 1
	}
	df.ctx.local_nets_ipv4_num = C.uint8_t(local_nets_ipv4_index)

	local_nets_ipv6_index := 0
	for _, n := range df.LocalNetsIpv6 {
		_, ipnet, err := net.ParseCIDR(n)
		if err != nil {
			continue
		}
		for i := 0; i < 16; i += 1 {
			df.ctx.local_nets_ipv6_pfix[local_nets_ipv6_index][i] = C.uint8_t(ipnet.IP[i])
		}
		ones, _ := ipnet.Mask.Size()
		df.ctx.local_nets_ipv6_plen[local_nets_ipv6_index] = C.uint8_t(ones)
		local_nets_ipv6_index += 1
	}
	df.ctx.local_nets_ipv6_num = C.uint8_t(local_nets_ipv6_index)

	aggregateFlagsIncoming, _ := aggregateFlags(df.AggregateIncoming)
	df.ctx.aggregate_flags_incoming = C.uint32_t(aggregateFlagsIncoming)
	aggregateFlagsOutgoing, _ := aggregateFlags(df.AggregateOutgoing)
	df.ctx.aggregate_flags_outgoing = C.uint32_t(aggregateFlagsOutgoing)
	aggregateFlagsInternal, _ := aggregateFlags(df.AggregateInternal)
	df.ctx.aggregate_flags_internal = C.uint32_t(aggregateFlagsInternal)
	aggregateFlagsExternal, _ := aggregateFlags(df.AggregateExternal)
	df.ctx.aggregate_flags_external = C.uint32_t(aggregateFlagsExternal)

	C.strcpy(&df.ctx.mrt_rib_path[0], C.CString(df.MrtRibPath))

	for i, c := range df.Cores {
		ctx_core := &df.ctx.cores[i]
		ctx_core.index = C.int(c.Index)
		for j, p := range c.Ports {
			ctx_port := &ctx_core.ports[j]
			ctx_port.index = C.uint8_t(p.Index)
			ctx_port.port_vlan_id = C.int32_t(p.PortVlanId)
			for i, v := range p.TagVlanIds {
				ctx_port.tag_vlan_ids[i] = C.int32_t(v)
			}
			ctx_port.tag_vlan_num = C.int(len(p.TagVlanIds))
		}
		ctx_core.port_num = C.uint8_t(len(c.Ports))
	}
	df.ctx.core_num = C.uint8_t(len(df.Cores))

	go func() {
		C.start(df.ctx)
	}()

	go func() {
		for {
			if (df.ctx.done) == 1 {
				break
			}
			if C.int(df.ctx.running) == 1 {
				C.check_and_reload_tables(df.ctx)
			}
			time.Sleep(time.Second)
		}
	}()

	go func() {
		for {
			if (df.ctx.done) == 1 {
				break
			}
			if C.int(df.ctx.running) == 1 {
				C.print_stats(df.ctx)
			}
			time.Sleep(10 * time.Second)
		}
	}()

	return nil
}

func (df *DpdkFlow) Stop() {
	fmt.Println("DpdkFlow.Stop()")
	df.ctx.done = 1
}

func init() {
	inputs.Add("dpdkflow", func() telegraf.Input {
		return NewDpdkFlow()
	})
}
