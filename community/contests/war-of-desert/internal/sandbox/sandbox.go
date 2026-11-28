// Package sandbox 给选手进程加操作系统级硬约束。
//
// 版本：v1.4
// 功能：Windows Job Object 封装——限制内存、禁止派生子进程、
// 随裁判退出连带杀死。裁判（internal/judge）与选手侧自检工具
// （cmd/sandbox）共用同一份实现，保证「赛场上的限制」
// 和「选手自测到的限制」是同一套数值。
//
// 能挡：子进程派生、内存爆炸、孤儿进程。
// 挡不住：文件读写、出网——那需要独立低权账户或容器。
package sandbox

// Limits 是一组沙箱限制。零值字段取 DefaultLimits 的默认值。
type Limits struct {
	// MemoryBytes 单进程内存上限
	MemoryBytes uint64
	// JobMemoryBytes 整个 Job 内存上限
	JobMemoryBytes uint64
	// MaxProcesses 允许的活跃进程数（1 = 不许派生子进程）
	MaxProcesses uint32
}

// DefaultLimits 是正式赛使用的限制。
func DefaultLimits() Limits {
	return Limits{
		MemoryBytes:    512 << 20,
		JobMemoryBytes: 1024 << 20,
		MaxProcesses:   1,
	}
}

func (l Limits) normalized() Limits {
	if l.MemoryBytes == 0 {
		l.MemoryBytes = DefaultLimits().MemoryBytes
	}
	if l.JobMemoryBytes == 0 {
		l.JobMemoryBytes = DefaultLimits().JobMemoryBytes
	}
	if l.MaxProcesses == 0 {
		l.MaxProcesses = DefaultLimits().MaxProcesses
	}
	return l
}

// Sandbox 是一个已创建、尚未绑定进程的沙箱。
type Sandbox struct {
	lim    Limits
	handle uintptr
	closed bool
}

// New 按给定限制创建沙箱。调用方用完必须 Close。
func New(lim Limits) *Sandbox {
	return newSandbox(lim.normalized())
}
