//go:build windows

// 文件：internal/sandbox/sandbox_windows.go
// 版本：v1.4
// 功能：Windows Job Object 实现。
//
// 结构体布局对照 JOBOBJECT_EXTENDED_LIMIT_INFORMATION（x64 共 144 字节）。
// 布局写错时 SetInformationJobObject 不会报错、限制却一条都不生效，
// 所以 internal/judge 下有一条「沙箱内必须无法派生子进程」的测试兜底。
package sandbox

import (
	"fmt"
	"os"
	"syscall"
	"unsafe"
)

const (
	jobObjectExtendedLimitInformation = 9

	limProcessMemory  = 0x00000100
	limJobMemory      = 0x00000200
	limActiveProcess  = 0x00000008
	limKillOnJobClose = 0x00002000

	procSetQuota  = 0x0100
	procTerminate = 0x0001
)

var (
	modkernel32                  = syscall.NewLazyDLL("kernel32.dll")
	procCreateJobObjectW         = modkernel32.NewProc("CreateJobObjectW")
	procSetInformationJobObject  = modkernel32.NewProc("SetInformationJobObject")
	procAssignProcessToJobObject = modkernel32.NewProc("AssignProcessToJobObject")
	procCloseHandle              = modkernel32.NewProc("CloseHandle")
)

type ioCounters struct {
	ReadOperationCount  uint64
	WriteOperationCount uint64
	OtherOperationCount uint64
	ReadTransferCount   uint64
	WriteTransferCount  uint64
	OtherTransferCount  uint64
}

type basicLimitInformation struct {
	PerProcessUserTimeLimit int64
	PerJobUserTimeLimit     int64
	LimitFlags              uint32
	_                       uint32
	MinimumWorkingSetSize   uintptr
	MaximumWorkingSetSize   uintptr
	ActiveProcessLimit      uint32
	_                       uint32
	Affinity                uintptr
	PriorityClass           uint32
	SchedulingClass         uint32
}

type extendedLimitInformation struct {
	BasicLimitInformation basicLimitInformation
	IoInfo                ioCounters
	ProcessMemoryLimit    uintptr
	JobMemoryLimit        uintptr
	PeakProcessMemoryUsed uintptr
	PeakJobMemoryUsed     uintptr
}

func newSandbox(lim Limits) *Sandbox {
	h, _, _ := procCreateJobObjectW.Call(0, 0)
	return &Sandbox{lim: lim, handle: h}
}

// Apply 把限制写进 Job 并把进程绑进去。
func (s *Sandbox) Apply(p *os.Process) error {
	if s == nil || s.closed {
		return fmt.Errorf("沙箱已关闭")
	}
	if s.handle == 0 {
		return fmt.Errorf("创建 Job Object 失败（沙箱未生效）")
	}
	info := extendedLimitInformation{
		BasicLimitInformation: basicLimitInformation{
			LimitFlags:         limProcessMemory | limJobMemory | limActiveProcess | limKillOnJobClose,
			ActiveProcessLimit: s.lim.MaxProcesses,
		},
		ProcessMemoryLimit: uintptr(s.lim.MemoryBytes),
		JobMemoryLimit:     uintptr(s.lim.JobMemoryBytes),
	}
	r, _, err := procSetInformationJobObject.Call(
		s.handle,
		uintptr(jobObjectExtendedLimitInformation),
		uintptr(unsafe.Pointer(&info)),
		unsafe.Sizeof(info),
	)
	if r == 0 {
		return fmt.Errorf("SetInformationJobObject 失败: %v", err)
	}
	ph, err := syscall.OpenProcess(procSetQuota|procTerminate, false, uint32(p.Pid))
	if err != nil {
		return fmt.Errorf("打开进程句柄失败: %w", err)
	}
	defer syscall.CloseHandle(ph)
	r, _, err = procAssignProcessToJobObject.Call(s.handle, uintptr(ph))
	if r == 0 {
		return fmt.Errorf("AssignProcessToJobObject 失败: %v", err)
	}
	return nil
}

// Close 释放句柄；KILL_ON_JOB_CLOSE 会连带杀掉残留进程。
func (s *Sandbox) Close() {
	if s != nil && !s.closed && s.handle != 0 {
		procCloseHandle.Call(s.handle)
		s.handle = 0
		s.closed = true
	}
}

// Available 报告当前平台是否支持沙箱。
func Available() bool { return true }

// Describe 返回可读的限制说明（GUI / CLI 展示用）。
func Describe(lim Limits) string {
	lim = lim.normalized()
	return fmt.Sprintf("单进程内存 %d MiB ｜ Job 内存 %d MiB ｜ 活跃进程上限 %d",
		lim.MemoryBytes>>20, lim.JobMemoryBytes>>20, lim.MaxProcesses)
}
