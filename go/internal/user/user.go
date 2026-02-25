package user

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unicode"
)

// ValidUsername returns true if the username contains only allowed characters:
// alphanumeric, '.', '-', '_'.
func ValidUsername(s string) bool {
	if s == "" {
		return false
	}
	for _, r := range s {
		if !unicode.IsLetter(r) && !unicode.IsDigit(r) && r != '.' && r != '-' && r != '_' {
			return false
		}
	}
	return true
}

// Exists returns true if data/<username>/user.conf is present.
func Exists(dataDir, username string) bool {
	_, err := os.Stat(filepath.Join(dataDir, username, "user.conf"))
	return err == nil
}

// CheckPassword returns true if the stored password matches.
func CheckPassword(dataDir, username, password string) bool {
	path := filepath.Join(dataDir, username, "user.conf")
	f, err := os.Open(path)
	if err != nil {
		return false
	}
	defer f.Close()

	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		key, val, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		if strings.TrimSpace(key) == "password" {
			return strings.TrimSpace(val) == password
		}
	}
	return false
}

// Create creates the user directory, user.conf, roster.xml, and offline/.
// Returns an error if the username is invalid, the user already exists, or
// any file system operation fails.
func Create(dataDir, username, password string) error {
	if !ValidUsername(username) {
		return fmt.Errorf("invalid-username")
	}
	if Exists(dataDir, username) {
		return fmt.Errorf("conflict")
	}

	userDir := filepath.Join(dataDir, username)
	if err := os.Mkdir(userDir, 0755); err != nil {
		return fmt.Errorf("mkdir: %w", err)
	}

	conf := fmt.Sprintf("password = %s\n", password)
	if err := os.WriteFile(filepath.Join(userDir, "user.conf"), []byte(conf), 0644); err != nil {
		return err
	}

	roster := "<?xml version=\"1.0\"?>\n<roster/>\n"
	if err := os.WriteFile(filepath.Join(userDir, "roster.xml"), []byte(roster), 0644); err != nil {
		return err
	}

	if err := os.Mkdir(filepath.Join(userDir, "offline"), 0755); err != nil {
		return err
	}

	return nil
}

// ChangePassword overwrites the password for an existing user.
func ChangePassword(dataDir, username, password string) error {
	path := filepath.Join(dataDir, username, "user.conf")
	conf := fmt.Sprintf("password = %s\n", password)
	return os.WriteFile(path, []byte(conf), 0644)
}

// Delete removes the user's entire data directory.
func Delete(dataDir, username string) error {
	return os.RemoveAll(filepath.Join(dataDir, username))
}
