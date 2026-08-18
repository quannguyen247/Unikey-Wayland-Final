package main

/*
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
*/
import "C"
import (
	"github.com/BambooEngine/bamboo-core"
)

var preeditor bamboo.IEngine
var currentMethod string = "Telex"
var rawKeys []rune

func initEngine(methodName string) {
	currentMethod = methodName
	defs := bamboo.GetInputMethodDefinitions()
	im := bamboo.ParseInputMethod(defs, currentMethod)
	preeditor = bamboo.NewEngine(im, uint(bamboo.VietnameseMode))
	rawKeys = make([]rune, 0)
}

//export Bamboo_Init
func Bamboo_Init() {
	initEngine("Telex")
}

//export Bamboo_SetInputMethod
func Bamboo_SetInputMethod(method C.int) {
	methodName := "Telex"
	switch int(method) {
	case 1:
		methodName = "Telex"
	case 2:
		methodName = "VNI"
	case 3:
		methodName = "VIQR"
	case 4:
		methodName = "Telex 2"
	default:
		methodName = "Telex"
	}
	initEngine(methodName)
}

//export Bamboo_CanProcessKey
func Bamboo_CanProcessKey(key C.uint32_t) C.bool {
	if preeditor == nil {
		return C.bool(false)
	}
	return C.bool(preeditor.CanProcessKey(rune(key)))
}

//export Bamboo_ProcessKey
func Bamboo_ProcessKey(key C.uint32_t) {
	if preeditor != nil {
        rawKeys = append(rawKeys, rune(key))
		preeditor.ProcessKey(rune(key), bamboo.VietnameseMode)
	}
}

//export Bamboo_RemoveLastChar
func Bamboo_RemoveLastChar() {
	if preeditor != nil {
        if len(rawKeys) > 0 {
            rawKeys = rawKeys[:len(rawKeys)-1]
            preeditor.Reset()
            for _, k := range rawKeys {
                preeditor.ProcessKey(k, bamboo.VietnameseMode)
            }
        }
	}
}

//export Bamboo_GetPreeditString
func Bamboo_GetPreeditString() *C.char {
	if preeditor == nil {
		return C.CString("")
	}
	vnSeq := preeditor.GetProcessedString(bamboo.VietnameseMode)
    
	return C.CString(vnSeq)
}

//export Bamboo_GetCommitString
func Bamboo_GetCommitString() *C.char {
	// Commit string is the same as Preedit string right before reset
	return Bamboo_GetPreeditString()
}

//export Bamboo_Reset
func Bamboo_Reset() {
	if preeditor != nil {
        rawKeys = make([]rune, 0)
		preeditor.Reset()
	}
}

func main() {}
