keys:
	@if [ -f signing_key.pem ]; then \
		echo "signing_key.pem esiste gia' — rimuovi con 'make delete_all_keys' se vuoi rigenerare"; \
		exit 1; \
	fi
	openssl genrsa -out signing_key.pem 2048
	openssl rsa -in signing_key.pem -pubout -out signing_pub.pem
	python3 gen_pubkey_header.py signing_pub.pem boot_pubkey.h BOOT_PUBKEY_N
	mv boot_pubkey.h ../lib/crypto/boot_pubkey.h

delete_all_keys:
	rm *.pem
	rm ../lib/crypto/boot_pubkey.h
