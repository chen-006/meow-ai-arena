// 文件：internal/sandbox/sandbox_windows_test.go
// 版本：v1.4
// 功能：验证 Job Object 真的挡住了子进程派生。
//
// 这条测试防的是「沙箱写了但没生效」：SetInformationJobObject 的结构体
// 布局或 LimitFlags 写错时，代码照样编译、自检照样通过，限制却一条都没上。
package sandbox

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestSandboxBlocksChildProcess(t *testing.T) {
	if runtime.GOOS != "windows" {
		t.Skip("仅在 Windows 上验证 Job Object")
	}
	tmp := t.TempDir()
	exe := filepath.Join(tmp, "childspawn.exe")
	build := exec.Command("go", "build", "-o", exe, "./testdata/childspawn")
	build.Stderr = os.Stderr
	if err := build.Run(); err != nil {
		t.Fatalf("编译自检程序失败: %v", err)
	}

	run := func(withSandbox bool) string {
		cmd := exec.Command(exe)
		out, _ := cmd.StdoutPipe()
		if err := cmd.Start(); err != nil {
			t.Fatalf("启动失败: %v", err)
		}
		var sb *Sandbox
		if withSandbox {
			sb = New(DefaultLimits())
			if err := sb.Apply(cmd.Process); err != nil {
				cmd.Process.Kill()
				t.Fatalf("沙箱施加失败: %v", err)
			}
		}
		buf := make([]byte, 64)
		n, _ := out.Read(buf)
		_ = cmd.Wait()
		if sb != nil {
			sb.Close()
		}
		return strings.TrimSpace(string(buf[:n]))
	}

	if got := run(false); got != "SPAWN_OK" {
		t.Fatalf("对照组应能派生进程，实得 %q", got)
	}
	if got := run(true); got != "SPAWN_BLOCKED" {
		t.Fatalf("沙箱内仍派生了子进程（%q）——Job Object 限制没生效", got)
	}
}
