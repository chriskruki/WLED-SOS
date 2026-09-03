test: site_url_test
	./site_url_test

site_url_test: site_url_test.cpp ../ndef_url.cpp
	$(CXX) -std=c++17 -Wall -Wextra -I.. -o $@ $^

clean:
	rm -f site_url_test

.PHONY: test clean
