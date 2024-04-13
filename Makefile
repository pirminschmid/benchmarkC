all:
	cd testing && ./build_all.sh

check:
	cd testing && ./run_all.sh
  
clean:
	cd testing && ./cleanup.sh
 
