package protocol

import (
	"bytes"
	"encoding/base64"
	"testing"
)

// rle 是测试用的编码器，与冻结 oracle 的 encodeMapLayers 同格式。
func rle(src []byte) []byte {
	var out []byte
	for i := 0; i < len(src); {
		v := src[i]
		n := 1
		for i+n < len(src) && src[i+n] == v && n < 256 {
			n++
		}
		out = append(out, v, byte(n-1))
		i += n
	}
	return out
}

func TestDecodeMapLayers(t *testing.T) {
	const w, h = 4, 3
	n := w * h
	base := []byte{0, 0, 0, 3, 3, 1, 1, 1, 1, 4, 4, 4}
	surf := []byte{1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0}
	elev := make([]byte, n)
	for i := range elev {
		elev[i] = byte(i * 7)
	}
	m := EncodedMap{
		W: w, H: h, Enc: "rle+b64/1",
		Base: base64.StdEncoding.EncodeToString(rle(base)),
		Surf: base64.StdEncoding.EncodeToString(rle(surf)),
		Elev: base64.StdEncoding.EncodeToString(elev),
	}
	got, err := DecodeMapLayers(m)
	if err != nil {
		t.Fatalf("解码失败: %v", err)
	}
	if !bytes.Equal(got.Base, base) {
		t.Fatalf("base 不一致: %v vs %v", got.Base, base)
	}
	if !bytes.Equal(got.Surf, surf) {
		t.Fatalf("surf 不一致: %v vs %v", got.Surf, surf)
	}
	if !bytes.Equal(got.Elev, elev) {
		t.Fatalf("elev 不一致: %v vs %v", got.Elev, elev)
	}
}

func TestDecodeMapLayersRejectsUnknownEncoding(t *testing.T) {
	if _, err := DecodeMapLayers(EncodedMap{W: 1, H: 1, Enc: "raw"}); err == nil {
		t.Fatal("未知编码必须报错，不能静默降级")
	}
}

func TestWinnerName(t *testing.T) {
	seats := []string{"A", "B"}
	if got := WinnerName(-1, seats); got != "draw" {
		t.Fatalf("平局应为 draw，得到 %s", got)
	}
	if got := WinnerName(1, seats); got != "B" {
		t.Fatalf("座位 1 应为 B，得到 %s", got)
	}
}

func TestDecodeMapLayersRejectsLengthMismatch(t *testing.T) {
	const w, h = 4, 3
	n := w * h
	m := EncodedMap{
		W: w, H: h, Enc: "rle+b64/1",
		Base: base64.StdEncoding.EncodeToString(rle([]byte{0, 1, 2})), // 只覆盖 3 格
		Surf: base64.StdEncoding.EncodeToString(rle(make([]byte, n))),
		Elev: base64.StdEncoding.EncodeToString(make([]byte, n)),
	}
	if _, err := DecodeMapLayers(m); err == nil {
		t.Fatal("base 层长度不足必须报错，不能补零")
	}
	m.Base = base64.StdEncoding.EncodeToString(rle(make([]byte, n)))
	m.Elev = base64.StdEncoding.EncodeToString(make([]byte, n-1))
	if _, err := DecodeMapLayers(m); err == nil {
		t.Fatal("elev 层长度不符必须报错，不能截断")
	}
}
