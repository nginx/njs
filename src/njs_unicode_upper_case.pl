#!/usr/bin/perl

use warnings;
use strict;

# BLOCK_SIZE should be 128, 256, 512, etc.  The value 128 provides
# the minimum memory footprint for both 32-bit and 64-bit platforms.
use constant BLOCK_SIZE => 128;

my %upper_case;
my %blocks;
my $max_block = 0;
my $max_upper_case = 0;

while (<>) {
    my @line = split(";", $_);

    if ($line[12]) {
        my ($symbol, $folding) = (hex $line[0], hex $line[12]);

        $upper_case{$symbol} = $folding;
        $blocks{int($symbol / BLOCK_SIZE)} = 1;

        if ($max_upper_case < $symbol) {
            $max_upper_case = $symbol;
        }
    }
}


my $last_block_size = $max_upper_case % BLOCK_SIZE + 1;


for my $block (sort { $a <=> $b } keys %blocks) {
   if ($max_block < $block) {
       $max_block = $block;
   }
}


my $blocks = scalar keys %blocks;

printf("\n/*\n" .
       " * %d %s-bytes blocks, %d pointers.\n" .
       " * %d bytes on 32-bit platforms, %d bytes on 64-bit platforms.\n" .
       " */\n\n",
       $blocks, BLOCK_SIZE, $max_block + 1,
       ($blocks - 1) * BLOCK_SIZE * 4 + $last_block_size + $max_block * 4,
       ($blocks - 1) * BLOCK_SIZE * 4 + $last_block_size + $max_block * 8);

printf("#define NJS_UNICODE_MAX_UPPER_CASE  0x%05x\n\n", $max_upper_case);
printf("#define NJS_UNICODE_BLOCK_SIZE      %d\n\n\n", BLOCK_SIZE);


for my $block (sort { $a <=> $b } keys %blocks) {
    my $block_size = ($block != $max_block) ? BLOCK_SIZE : $last_block_size;

    print "static const uint32_t  ";
    printf("njs_unicode_upper_case_block_%03x[%d]\n" .
           "    njs_aligned(64) =\n" .
           "{",
           $block, $block_size);

    for my $c (0 .. $block_size - 1) {
        printf "\n   " if $c % 8 == 0;

        my $n = $block * BLOCK_SIZE + $c;

        if (exists $upper_case{$n}) {
            printf(" 0x%05x,", $upper_case{$n});

        } else {
            #print " .......,";
            printf(" 0x%05x,", $n);
        }
    }

    print "\n};\n\n\n";
}


print "static const uint32_t  *njs_unicode_upper_case_blocks[]\n" .
      "    njs_aligned(64) =\n" .
      "{\n";

for my $block (0 .. $max_block) {
    if (exists($blocks{$block})) {
        printf("    njs_unicode_upper_case_block_%03x,\n", $block);

    } else {
        print  "    NULL,\n";
    }
}

print "};\n";
