.PHONY: test sample decode clean

test: sample
	ruby test/run_all.rb

sample:
	ruby resources/generate_sample_pcap.rb

decode: sample
	ruby bin/decode_pcap.rb resources/sample.pcap

clean:
	rm -f resources/sample.pcap
