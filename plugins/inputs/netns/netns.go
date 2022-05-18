package netns

import (
	"bufio"
	"bytes"
	"fmt"
	"os/exec"
	"regexp"
	"strconv"
	"time"

	"github.com/influxdata/telegraf"
	"github.com/influxdata/telegraf/config"
	"github.com/influxdata/telegraf/internal"
	"github.com/influxdata/telegraf/plugins/inputs"
)

type Netns struct {
	Netns                   string          `toml:"netns"`
	Timeout                 config.Duration `toml:"timeout"`
	CollectIpv4RouteCount   bool            `toml:"collect_ipv4_route_count"`
	CollectIpv6RouteCount   bool            `toml:"collect_ipv6_route_count"`
	Ipv4RouteCountProtocols []string        `toml:"ipv4_route_count_protocols"`
	Ipv6RouteCountProtocols []string        `toml:"ipv6_route_count_protocols"`
}

const measurement = "netns"

var defaultTimeout = config.Duration(time.Second)

var reIfStats = regexp.MustCompile(`^\s*(vlan\d+):\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$`)

var reIpRouteCountProtocol = regexp.MustCompile(`^\s*(\d+) of .* networks in table master(\d)`)

func (n *Netns) Init() error {
	_, err := exec.LookPath("ip")
	if err != nil {
		return err
	}

	return nil
}

func (n *Netns) gatherIfStats(acc telegraf.Accumulator) {
	ipPath, err := exec.LookPath("ip")
	if err != nil {
		return
	}

	cmdName := ipPath
	cmd := exec.Command(cmdName, "netns", "exec", n.Netns, "cat", "/proc/net/dev")

	var out bytes.Buffer
	cmd.Stdout = &out
	err = internal.RunTimeout(cmd, time.Duration(n.Timeout))
	if err != nil {
		return
	}

	scanner := bufio.NewScanner(&out)
	for scanner.Scan() {
		line := scanner.Text()
		result := reIfStats.FindStringSubmatch(line)
		if result == nil || len(result) != 18 {
			continue
		}
		rx_bytes, _ := strconv.ParseUint(result[2], 10, 64)
		rx_packets, _ := strconv.ParseUint(result[3], 10, 64)
		rx_errs, _ := strconv.ParseUint(result[4], 10, 64)
		rx_drop, _ := strconv.ParseUint(result[5], 10, 64)
		rx_fifo, _ := strconv.ParseUint(result[6], 10, 64)
		rx_frame, _ := strconv.ParseUint(result[7], 10, 64)
		rx_compressed, _ := strconv.ParseUint(result[8], 10, 64)
		rx_multicast, _ := strconv.ParseUint(result[9], 10, 64)
		tx_bytes, _ := strconv.ParseUint(result[10], 10, 64)
		tx_packets, _ := strconv.ParseUint(result[11], 10, 64)
		tx_errs, _ := strconv.ParseUint(result[12], 10, 64)
		tx_drop, _ := strconv.ParseUint(result[13], 10, 64)
		tx_fifo, _ := strconv.ParseUint(result[14], 10, 64)
		tx_colls, _ := strconv.ParseUint(result[15], 10, 64)
		tx_carrier, _ := strconv.ParseUint(result[16], 10, 64)
		tx_compressed, _ := strconv.ParseUint(result[17], 10, 64)
		tags := map[string]string{
			"interface": result[1],
		}
		fields := map[string]interface{}{
			"rx_bytes":      rx_bytes,
			"rx_packets":    rx_packets,
			"rx_errs":       rx_errs,
			"rx_drop":       rx_drop,
			"rx_fifo":       rx_fifo,
			"rx_frame":      rx_frame,
			"rx_compressed": rx_compressed,
			"rx_multicast":  rx_multicast,
			"tx_bytes":      tx_bytes,
			"tx_packets":    tx_packets,
			"tx_errs":       tx_errs,
			"tx_drop":       tx_drop,
			"tx_fifo":       tx_fifo,
			"tx_colls":      tx_colls,
			"tx_carrier":    tx_carrier,
			"tx_compressed": tx_compressed,
		}
		acc.AddCounter(measurement, fields, tags)
	}

}

func (n *Netns) gatherIpRouteCountProtocol(acc telegraf.Accumulator, ipVer int, protocol string) {
	birdcPath, err := exec.LookPath("birdc")
	if err != nil {
		return
	}

	cmdName := birdcPath
	cmd := exec.Command(cmdName, "show", "route", "protocol", protocol, "count")

	var out bytes.Buffer
	cmd.Stdout = &out
	err = internal.RunTimeout(cmd, time.Duration(n.Timeout))
	if err != nil {
		return
	}

	scanner := bufio.NewScanner(&out)
	for scanner.Scan() {
		line := scanner.Text()
		result := reIpRouteCountProtocol.FindStringSubmatch(line)
		if result == nil || len(result) != 3 {
			continue
		}
		routeCount, _ := strconv.ParseUint(result[1], 10, 64)
		masterVer, _ := strconv.Atoi(result[2])
		if masterVer != ipVer {
			continue
		}
		tags := map[string]string{
			"protocol": protocol,
		}
		fields := map[string]interface{}{
			fmt.Sprintf("ip%d_routes", ipVer): routeCount,
		}
		acc.AddGauge(measurement, fields, tags)
	}
}

func (n *Netns) Gather(acc telegraf.Accumulator) error {
	n.gatherIfStats(acc)
	if n.CollectIpv4RouteCount {
		for _, protocol := range n.Ipv4RouteCountProtocols {
			n.gatherIpRouteCountProtocol(acc, 4, protocol)
		}
	}
	if n.CollectIpv6RouteCount {
		for _, protocol := range n.Ipv6RouteCountProtocols {
			n.gatherIpRouteCountProtocol(acc, 6, protocol)
		}
	}
	return nil
}

func init() {
	inputs.Add("netns", func() telegraf.Input {
		return &Netns{
			Timeout: defaultTimeout,
		}
	})
}
