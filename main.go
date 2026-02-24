package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"jabber/internal/server"
	"log/slog"
	"os"
	"os/signal"
	"strings"
	"syscall"
)

type config struct {
	domain      string
	port        int
	bindAddress string
	dataDir     string
	logFile     string
	logLevel    string
}

func defaults() config {
	return config{
		domain:      "localhost",
		port:        5222,
		bindAddress: "0.0.0.0",
		dataDir:     "./data",
		logFile:     "./xmppd.log",
		logLevel:    "INFO",
	}
}

func loadFile(path string, cfg *config) error {
	f, err := os.Open(path)
	if err != nil {
		return err
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
		key = strings.TrimSpace(key)
		val = strings.TrimSpace(val)
		switch key {
		case "domain":
			cfg.domain = val
		case "port":
			fmt.Sscanf(val, "%d", &cfg.port)
		case "bind_address":
			cfg.bindAddress = val
		case "datadir":
			cfg.dataDir = val
		case "logfile":
			cfg.logFile = val
		case "loglevel":
			cfg.logLevel = val
		}
	}
	return sc.Err()
}

func setupLogging(cfg *config) {
	var level slog.Level
	switch strings.ToUpper(cfg.logLevel) {
	case "DEBUG":
		level = slog.LevelDebug
	case "WARN":
		level = slog.LevelWarn
	case "ERROR":
		level = slog.LevelError
	default:
		level = slog.LevelInfo
	}

	var w *os.File
	if cfg.logFile != "" {
		var err error
		w, err = os.OpenFile(cfg.logFile, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			w = os.Stderr
			slog.Warn("could not open log file, using stderr", "path", cfg.logFile, "err", err)
		}
	} else {
		w = os.Stderr
	}

	h := slog.NewTextHandler(w, &slog.HandlerOptions{Level: level})
	slog.SetDefault(slog.New(h))
}

func main() {
	cfg := defaults()

	// First pass: find -c flag for config file path.
	configFile := ""
	for i, arg := range os.Args[1:] {
		if arg == "-c" && i+1 < len(os.Args[1:]) {
			configFile = os.Args[i+2]
			break
		}
	}
	if configFile != "" {
		_ = loadFile(configFile, &cfg)
	} else {
		_ = loadFile("./xmppd.conf", &cfg) // optional default
	}

	// Second pass: CLI flags override config file.
	fs := flag.NewFlagSet("xmppd", flag.ExitOnError)
	fs.String("c", configFile, "config file path")
	fs.StringVar(&cfg.domain, "d", cfg.domain, "server domain")
	fs.IntVar(&cfg.port, "p", cfg.port, "listen port")
	fs.StringVar(&cfg.dataDir, "D", cfg.dataDir, "data directory")
	fs.StringVar(&cfg.logFile, "l", cfg.logFile, "log file path")
	fs.StringVar(&cfg.logLevel, "L", cfg.logLevel, "log level (DEBUG/INFO/WARN/ERROR)")
	_ = fs.Parse(os.Args[1:])

	setupLogging(&cfg)

	addr := fmt.Sprintf("%s:%d", cfg.bindAddress, cfg.port)
	srv := server.New(cfg.domain, cfg.dataDir, addr)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		sig := <-sigCh
		slog.Info("shutting down", "signal", sig)
		cancel()
	}()

	if err := srv.Run(ctx); err != nil {
		slog.Error("server error", "err", err)
		os.Exit(1)
	}
}
