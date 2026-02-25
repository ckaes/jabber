package main

import (
	"flag"
	"fmt"
	"jabber/internal/user"
	"os"
)

func main() {
	fs := flag.NewFlagSet("useradd", flag.ExitOnError)
	dataDir := fs.String("d", "", "data directory (required)")
	username := fs.String("u", "", "username (required)")
	password := fs.String("p", "", "password (required)")
	domain := fs.String("D", "localhost", "domain (for display only)")
	_ = fs.Parse(os.Args[1:])

	if *dataDir == "" || *username == "" || *password == "" {
		fmt.Fprintln(os.Stderr, "Error: -d, -u, and -p are required.")
		fs.Usage()
		os.Exit(1)
	}

	if !user.ValidUsername(*username) {
		fmt.Fprintf(os.Stderr, "Error: invalid username '%s'. "+
			"Only alphanumeric, '.', '-', '_' allowed.\n", *username)
		os.Exit(1)
	}

	if err := user.Create(*dataDir, *username, *password); err != nil {
		switch err.Error() {
		case "conflict":
			fmt.Fprintf(os.Stderr, "Error: user '%s@%s' already exists.\n", *username, *domain)
		default:
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		}
		os.Exit(1)
	}

	fmt.Printf("User '%s@%s' created successfully.\n", *username, *domain)
}
