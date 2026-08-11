
VER =		$(shell grep 'define NJS_VERSION ' src/njs.h		\
			| sed -e 's/^.*"\(.*\)".*/\1/')
NJS =		njs-$(VER)
TARBALL =	$(NJS).tar.gz


release:
	git archive --format=tar.gz --prefix=$(NJS)/ -o $(TARBALL) HEAD

	sha256sum $(TARBALL) > $(TARBALL).sha256

	@echo "excluded from $(TARBALL):"
	@{ git ls-tree -r --name-only HEAD;				\
	   tar -t -z -f $(TARBALL) | grep -v "/$$"			\
		| sed -e "s|^$(NJS)/||"; }				\
	| sort | uniq -u


# Builds the extracted tarball in a temporary directory, which is kept
# around on failure.

release-check:
	@dir=`mktemp -d`;						\
	echo "extracting $(TARBALL) into $$dir";			\
	tar -x -z -f $(TARBALL) -C $$dir				\
	&& cd $$dir/$(NJS)						\
	&& ./configure							\
	&& $(MAKE) njs							\
	&& $(MAKE) unit_test						\
	&& rm -rf $$dir							\
	&& echo "$(TARBALL) builds"


.PHONY: release release-check
