package fsroot

import "os"

// VIC_FS_ROOT
func RobotPath(path string) string {
	root := os.Getenv("VIC_FS_ROOT")
	if root == "" {
		return path
	}
	return root + path
}
