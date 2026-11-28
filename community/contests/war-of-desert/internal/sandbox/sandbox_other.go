//go:build !windows

// 文件：internal/sandbox/sandbox_other.go
// 版本：v1.4
// 功能：非 Windows 平台的沙箱占位。
//
// 正式赛执行链跑在 Windows 上（Job Object）。其他平台需要外部约束
// （容器 / 独立低权账户 / cgroup），这里显式报告不可用，
// 让调用方写进赛务记录，而不是误以为已经隔离。
package sandbox

import (
	"errors"
	"fmt"
	"os"
)

func newSandbox(lim Limits) *Sandbox { return &Sandbox{lim: lim} }

// Apply 在非 Windows 平台直接报错：宁可不跑，也不要假装隔离。
func (s *Sandbox) Apply(p *os.Process) error {
	return errors.New("当前平台没有内置沙箱：需要容器或独立低权账户")
}

func (s *Sandbox) Close() {}

// Available 报告当前平台是否支持沙箱。
func Available() bool { return false }

// Describe 返回可读的限制说明。
func Describe(lim Limits) string {
	lim = lim.normalized()
	return fmt.Sprintf("（当前平台不可用）单进程内存 %d MiB ｜ Job 内存 %d MiB ｜ 活跃进程上限 %d",
		lim.MemoryBytes>>20, lim.JobMemoryBytes>>20, lim.MaxProcesses)
}
