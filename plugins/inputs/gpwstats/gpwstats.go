package gpwstats

import (
	"bufio"
	"bytes"
	"os/exec"
	"regexp"
	"strconv"
	"time"

	"github.com/influxdata/telegraf"
	"github.com/influxdata/telegraf/config"
	"github.com/influxdata/telegraf/internal"
	"github.com/influxdata/telegraf/plugins/inputs"
)

type Gpwstats struct {
	Timeout config.Duration
}

const measurement = "gpwstats"

var defaultTimeout = config.Duration(time.Second)

var re = regexp.MustCompile(`^(\S+)\s+(\d+)$`)

func (g *Gpwstats) Init() error {
	_, err := exec.LookPath("gpwstats")
	if err != nil {
		return err
	}

	return nil
}

func (g *Gpwstats) Gather(acc telegraf.Accumulator) error {
	gpwstatsPath, err := exec.LookPath("gpwstats")
	if err != nil {
		return err
	}

	var args []string
	cmdName := gpwstatsPath
	cmd := exec.Command(cmdName, args...)

	var out bytes.Buffer
	cmd.Stdout = &out
	err = internal.RunTimeout(cmd, time.Duration(g.Timeout))
	if err != nil {
		return err
	}

	fields := map[string]interface{}{}
	tags := map[string]string{}
	scanner := bufio.NewScanner(&out)
	for scanner.Scan() {
		line := scanner.Text()
		result := re.FindStringSubmatch(line)
		if result == nil || len(result) != 3 {
			continue
		}
		val, err := strconv.ParseUint(result[2], 10, 64)
		if err != nil {
			continue
		}
		fields[result[1]] = val
	}
	acc.AddCounter(measurement, fields, tags)

	return nil
}

func init() {
	inputs.Add("gpwstats", func() telegraf.Input {
		return &Gpwstats{
			Timeout: defaultTimeout,
		}
	})
}
