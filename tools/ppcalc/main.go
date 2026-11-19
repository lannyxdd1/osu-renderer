// Command ppcalc computes osu!standard performance points for a score.
//
// It links against danser-go, so the numbers match the PP counter that danser
// draws in the rendered video instead of an approximated formula. danser is
// pinned to the version the app expects; bump the requirement in go.mod when
// you update the danser binary.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"

	"github.com/wieku/danser-go/app/beatmap"
	"github.com/wieku/danser-go/app/beatmap/difficulty"
	"github.com/wieku/danser-go/app/rulesets/osu/performance"
	"github.com/wieku/danser-go/app/rulesets/osu/performance/api"
	"github.com/wieku/danser-go/framework/env"
)

func fail(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "ppcalc: "+format+"\n", args...)
	os.Exit(1)
}

func main() {
	env.Init("osu-render")

	mapPath := flag.String("map", "", "path to the .osu file (required)")
	mods := flag.String("mods", "", "mod acronyms, e.g. HDHR (NC implies DT)")
	accuracy := flag.Float64("acc", 0, "accuracy in percent, e.g. 98.21")
	combo := flag.Int("combo", 0, "highest combo of the play")
	misses := flag.Int("miss", 0, "miss count")
	n300 := flag.Int("n300", 0, "300 count")
	n100 := flag.Int("n100", 0, "100 count")
	n50 := flag.Int("n50", 0, "50 count")
	score := flag.Int("score", 0, "score value")
	flag.Parse()

	if *mapPath == "" {
		flag.Usage()
		os.Exit(2)
	}

	file, err := os.Open(*mapPath)
	if err != nil {
		fail("%v", err)
	}
	defer file.Close()

	bMap := beatmap.ParseBeatMapFile(file)
	if bMap == nil {
		fail("cannot parse %s", *mapPath)
	}
	defer bMap.Clear()

	// Same order as danser's playback path: mods first, then objects with
	// colours, otherwise slider ends and stacking differ and so do the stars.
	bMap.Diff.SetMods(difficulty.ParseMods(*mods))
	beatmap.ParseTimingPointsAndPauses(bMap)
	beatmap.ParseObjects(bMap, false, true)

	attrs := performance.GetDifficultyCalculator().CalculateSingle(bMap, bMap.Diff)
	results := performance.CreatePPCalculator().Calculate(attrs, api.PerfScore{
		Score:      *score,
		Accuracy:   *accuracy,
		MaxCombo:   *combo,
		CountGreat: *n300,
		CountOk:    *n100,
		CountMeh:   *n50,
		CountMiss:  *misses,
	}, bMap.Diff)

	out := map[string]any{
		"pp":           results.Total,
		"ppAim":        results.Aim,
		"ppSpeed":      results.Speed,
		"ppAcc":        results.Acc,
		"ppReading":    results.Cognition,
		"stars":        attrs.Total,
		"starsAim":     attrs.Aim,
		"starsSpeed":   attrs.Speed,
		"starsReading": attrs.Reading,
	}

	if err := json.NewEncoder(os.Stdout).Encode(out); err != nil {
		fail("%v", err)
	}
}
