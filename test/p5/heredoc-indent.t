#!perl
# <<~TAG removes the smallest indentation of the non-blank lines, the terminator may be indented
my $n = "W";
print <<~EOT;
    hello $n
      indented
    last
    EOT
#=> hello W
#=>   indented
#=> last
print <<~'RAW', "mid\n", <<~"Q";
	raw $n
	  tab
	RAW
    q $n
    Q
#=> raw $n
#=>   tab
#=> mid
#=> q W
