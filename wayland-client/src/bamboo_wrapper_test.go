package main

import (
	"testing"
	"unicode/utf8"

	"github.com/BambooEngine/bamboo-core"
)

func typeKeys(keys string) {
	for _, key := range keys {
		processKey(key)
	}
}

func resetTestEngine(method string, spellCheck, autoRestore bool) {
	currentFlags = bamboo.EfreeToneMarking
	spellCheckEnabled = spellCheck
	autoRestoreEnabled = autoRestore
	initEngine(method)
}

func TestTelexComposition(t *testing.T) {
	resetTestEngine("Telex", true, true)
	typeKeys("theer")
	if got := processedString(true); got != "thể" {
		t.Fatalf("theer: got %q, want %q", got, "thể")
	}
}

func TestInputMethodsSelectedBySettings(t *testing.T) {
	tests := []struct {
		method string
		keys   string
	}{
		{method: "VNI", keys: "the63"},
		{method: "VIQR", keys: "the^?"},
		{method: "Telex 2", keys: "theer"},
	}

	for _, tc := range tests {
		t.Run(tc.method, func(t *testing.T) {
			resetTestEngine(tc.method, true, true)
			typeKeys(tc.keys)
			if got := processedString(true); got != "thể" {
				t.Fatalf("%s: got %q, want %q", tc.keys, got, "thể")
			}
		})
	}
}

func TestInvalidVietnameseSequenceRestoresRawKeys(t *testing.T) {
	resetTestEngine("Telex", true, true)
	typeKeys("afc") // àc is invalid: c only accepts acute or dot tones.
	if got := processedString(true); got != "afc" {
		t.Fatalf("got %q, want raw keys %q", got, "afc")
	}
}

func TestAutoRestoreCanBeDisabled(t *testing.T) {
	resetTestEngine("Telex", false, false)
	typeKeys("afc")
	if got := processedString(true); got != "àc" {
		t.Fatalf("got %q, want un-restored composition %q", got, "àc")
	}
}

func TestBackspaceReplaysBambooStateCorrectly(t *testing.T) {
	resetTestEngine("Telex", true, true)
	typeKeys("theer")
	removeLastKey()
	if got := processedString(false); got != "thê" {
		t.Fatalf("got %q, want %q", got, "thê")
	}
}

func TestRapidCompositionAlwaysCommitsFullWord(t *testing.T) {
	tests := []struct {
		method string
		keys   string
		want   string
	}{
		{method: "Telex", keys: "nois", want: "nói"},
		{method: "VNI", keys: "noi1", want: "nói"},
		{method: "VNI", keys: "the63", want: "thể"},
		{method: "VNI", keys: "d9u7o7ng2", want: "đường"},
		{method: "VNI", keys: "tie61ng", want: "tiếng"},
		{method: "VNI", keys: "vie65t", want: "việt"},
		{method: "VIQR", keys: "noi'", want: "nói"},
		{method: "Telex 2", keys: "theer", want: "thể"},
	}

	for _, tc := range tests {
		t.Run(tc.method, func(t *testing.T) {
			for i := 0; i < 10000; i++ {
				resetTestEngine(tc.method, false, false)
				for _, key := range tc.keys {
					processKey(key)
					if got := processedString(false); !utf8.ValidString(got) {
						t.Fatalf("iteration %d: invalid UTF-8 preedit %q", i, got)
					}
				}
				if got := processedString(true); got != tc.want {
					t.Fatalf("iteration %d: got %q, want %q", i, got, tc.want)
				}
			}
		})
	}
}
