.PHONY: build
build: ## Build executable files
	g++ main.cpp -o mycurl -L $PWD/include/asio-1.18.2 -lboost_system -lboost_thread -lpthread

.PHONY: run
run: ## Run built files
	./mycurl