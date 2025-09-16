# Makefile for RAT project with automatic persistence

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude
LDFLAGS = 

# Directories
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
DOCS_DIR = docs

# Target executables
CLIENT_TARGET = $(BIN_DIR)/client
SERVER_TARGET = $(BIN_DIR)/server

# Source files
CLIENT_SRC = $(SRC_DIR)/client.c $(SRC_DIR)/persistence.c
SERVER_SRC = $(SRC_DIR)/server.c

# Object files (in src directory to keep things clean)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
SERVER_OBJ = $(SERVER_SRC:.c=.o)

# Default target
all: $(BIN_DIR) $(CLIENT_TARGET) $(SERVER_TARGET)

# Create bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Client executable
$(CLIENT_TARGET): $(CLIENT_OBJ) | $(BIN_DIR)
	$(CC) $(CLIENT_OBJ) -o $(CLIENT_TARGET) $(LDFLAGS)

# Server executable
$(SERVER_TARGET): $(SERVER_OBJ) | $(BIN_DIR)
	$(CC) $(SERVER_OBJ) -o $(SERVER_TARGET) $(LDFLAGS)

# Compile object files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(SRC_DIR)/*.o
	rm -rf $(BIN_DIR)

# Install targets (copies to common locations)
install-client: $(CLIENT_TARGET)
	cp $(CLIENT_TARGET) /usr/local/bin/rat-client || cp $(CLIENT_TARGET) ~/bin/rat-client || echo "Copy manually to desired location"

install-server: $(SERVER_TARGET)
	cp $(SERVER_TARGET) /usr/local/bin/rat-server || cp $(SERVER_TARGET) ~/bin/rat-server || echo "Copy manually to desired location"

# Development/testing targets
test-compile: all
	@echo "✅ Compilation successful!"
	@echo "   Client: $(CLIENT_TARGET)"
	@echo "   Server: $(SERVER_TARGET)"

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: all

# Release build (optimized)
release: CFLAGS += -O2 -DNDEBUG
release: all

# Create source distribution
dist: clean
	@echo "Creating source distribution..."
	tar -czf rat-$(shell date +%Y%m%d).tar.gz \
		$(SRC_DIR)/ $(INC_DIR)/ $(DOCS_DIR)/ \
		Makefile README.md LICENSE TODO.md \
		--exclude-vcs

# Package the project (binary distribution)
package: release
	@echo "Creating binary package..."
	mkdir -p package/rat
	cp -r $(BIN_DIR) package/rat/
	cp README.md LICENSE package/rat/
	cd package && tar -czf rat-bin-$(shell date +%Y%m%d).tar.gz rat/
	rm -rf package/rat

# Show project structure
structure:
	@echo "Project Structure:"
	@tree -I '.git|.vscode|*.o' || find . -type f ! -path './.git/*' ! -path './.vscode/*' ! -name '*.o' | sort

# Help target
help:
	@echo "Available targets:"
	@echo "  all            - Build both client and server (default)"
	@echo "  client         - Build only the client"
	@echo "  server         - Build only the server"
	@echo "  clean          - Remove all build files"
	@echo "  debug          - Build with debug symbols"
	@echo "  release        - Build optimized release version"
	@echo "  install-client - Install client to system path"
	@echo "  install-server - Install server to system path"
	@echo "  test-compile   - Test compilation and show output paths"
	@echo "  dist           - Create source distribution tarball"
	@echo "  package        - Create binary distribution package"
	@echo "  structure      - Show project directory structure"
	@echo "  help           - Show this help message"

.PHONY: all clean install-client install-server test-compile debug release dist package structure help