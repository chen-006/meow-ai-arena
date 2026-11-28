// 哈希与长度口径（rules.js:436-479 的复刻）。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：帧哈希 / rulesHash / 行长度上限三类口径。
//
// 两个口径刻意不同：
//   - FNV1A 按 **UTF-16 code unit** 迭代——与 JS String.charCodeAt 一致，
//     用于帧哈希 / rulesHash 等任何与 JS 字符串哈希对齐的场景。
//   - Utf8Len 按 UTF-8 字节数——协议行长度上限（MAX_LINE）用这个口径。
package sim

// FNV1A 按 UTF-16 code unit 迭代的 FNV-1a 32 位哈希，输出 8 位十六进制。
//
// JS 侧 rules.js:436-440 的逐位复刻：
// h ^= codeUnit; h = imul(h, 16777619)。
func FNV1A(s string) string {
	h := uint32(2166136261)
	for _, u := range utf16Units(s) {
		h ^= uint32(u)
		h = imul32(h, 16777619)
	}
	return hex8(h)
}

// FNV1AU32 是 FNV1A 的数值版本。
//
// 帧哈希链内部比较用，免一次十六进制编解码。
func FNV1AU32(s string) uint32 {
	h := uint32(2166136261)
	for _, u := range utf16Units(s) {
		h ^= uint32(u)
		h = imul32(h, 16777619)
	}
	return h
}

// utf16Units 把 UTF-8 字符串转成 JS 意义上的 UTF-16 code unit 序列。
//
// BMP 内直接取码点；超过 0xFFFF 的码点展开为高低代理对——
// JSON.stringify 在 JS 里输出的就是 UTF-16 序列，
// 行内中文极常见，不能按字节算。
func utf16Units(s string) []uint16 {
	units := make([]uint16, 0, len(s))
	for _, r := range s {
		switch {
		case r <= 0xFFFF:
			units = append(units, uint16(r))
		default:
			r2 := r - 0x10000
			units = append(units,
				uint16(0xD800+(r2>>10)),
				uint16(0xDC00+(r2&0x3FF)))
		}
	}
	return units
}

// hex8 输出 32 位整数的 8 位小写十六进制。
func hex8(h uint32) string {
	const digits = "0123456789abcdef"
	out := make([]byte, 8)
	for i := 7; i >= 0; i-- {
		out[i] = digits[h&0xF]
		h >>= 4
	}
	return string(out)
}

// Utf8Length 返回字符串的 UTF-8 字节长度。
//
// rules.js:469-479 的口径；协议行长度上限 MAX_LINE 按这个算。
func Utf8Length(s string) int {
	// Go 字符串本身就是 UTF-8 字节串
	return len(s)
}
