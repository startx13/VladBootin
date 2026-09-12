.PHONY: keys test_signature sign_bootloader delete_all_keys delete_all_signatures

# --- Generazione chiavi + header pubblico ---
#
# keys e' un alias phony comodo da chiamare ("make keys"), ma il
# lavoro vero dipende dal FILE signing_key.pem: se esiste gia',
# make non rigenera nulla e non fallisce.
keys: signing_key.pem

signing_key.pem:
	openssl genrsa -out signing_key.pem 2048
	openssl rsa -in signing_key.pem -pubout -out signing_pub.pem
	python3 gen_pubkey_header.py signing_pub.pem boot_pubkey.h BOOT_PUBKEY_N
	mv boot_pubkey.h ../lib/crypto/boot_pubkey.h

# --- Firma di test ---
#
# Dipende dal FILE signing_key.pem, non dal target phony keys:
# se la chiave esiste gia' la riusa, altrimenti la genera al volo.
test_signature: signing_key.pem
	printf '%s' "Testo Di Prova" > msg.bin
	openssl dgst -sha256 -sign signing_key.pem -out msg.sig msg.bin
	python3 gen_signature_header.py msg.sig test_signature.h signature_bytes
	mv test_signature.h ../lib/crypto/test_signature.h

# --- Firma dell'immagine reale del bootloader ---
#
# Dipende dalla chiave (la genera se manca). Richiede che
# vladBootin.elf e vladBootin.img siano gia' stati prodotti dal
# Makefile principale nella root del progetto (target 'link'):
# qui non si ricompila nulla, si patcha solo l'img esistente.
sign_bootloader: signing_key.pem
	@if [ ! -f ../build/vladBootin.elf ] || [ ! -f ../vladBootin.img ]; then \
		echo "ERRORE: ../build/vladBootin.elf o ../vladBootin.img mancanti."; \
		echo "Lancia prima 'make' nella root del progetto."; \
		exit 1; \
	fi
	python3 sign_bootloader.py ../build/vladBootin.elf ../vladBootin.img signing_key.pem

# --- Pulizia ---

delete_all_keys: delete_all_signatures
	rm -f *.pem
	rm -f ../lib/crypto/boot_pubkey.h

delete_all_signatures:
	rm -f msg.bin msg.sig ../lib/crypto/test_signature.h