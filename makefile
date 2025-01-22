build-all: local.out global.out

build-local: local.out

local.out:
	g++ ./local.cpp -o local.out

run-local: local.out
	./local.out $(PAGESIZE) $(FRAMES) $(POLICY) $(FILE)

clean-local:
	rm -f local.out


build-global: global.out

global.out:
	g++ ./global.cpp -o global.out

run-global: global.out
	./global.out $(PAGESIZE) $(FRAMES) $(POLICY) $(FILE)

clean-global:
	rm -f global.out

clean-all:
	rm -f local.out global.out