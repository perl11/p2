#!perl
# p2-only: shell and file system details differ per platform; not cross-checked with perl
# file system builtins: mkdir rmdir chdir rename link symlink readlink chmod utime truncate stat opendir readdir; (LIST)[i] slices
my $d = "fileops-tmp.$$";
say mkdir($d, 0755) ? "mkdir" : "no mkdir"; #=> mkdir
say -d $d ? "is dir" : "not dir"; #=> is dir
open(F, ">$d/a") || die; print F "hello"; close F;
say link("$d/a", "$d/b") ? "link" : "no link"; #=> link
my @s = stat("$d/a");
say scalar(@s), " $s[7] $s[3]"; #=> 13 5 2
say chmod(0600, "$d/a", "$d/b"); #=> 2
say sprintf("%o", (stat "$d/b")[2] & 0777); #=> 600
say rename("$d/a", "$d/c") ? "renamed" : "no rename"; #=> renamed
say utime(500000000, 500000001, "$d/c"); #=> 1
my ($at, $mt) = (stat "$d/c")[8, 9];
say "$at $mt"; #=> 500000000 500000001
say truncate("$d/c", 2), " ", -s "$d/c"; #=> 1 2
say symlink("c", "$d/l") ? readlink("$d/l") : "no symlink"; #=> c
opendir(D, $d) || die;
my @f = sort grep { $_ ne "." && $_ ne ".." } readdir(D);
say "@f"; #=> b c l
rewinddir(D);
my $n = 0;
while (my $e = readdir(D)) { $n++ }
say $n; #=> 5
closedir(D);
say unlink("$d/b", "$d/c", "$d/l"); #=> 3
say rmdir($d) ? "rmdir" : "no rmdir"; #=> rmdir
say mkdir("/nonexistent-dir/x") ? "bad" : "fail"; #=> fail
my @l = (10, 20, 30);
my ($p, $q) = (@l)[1, 2];
say "$p $q"; #=> 20 30
say((10, 20, 30)[-1]); #=> 30
my @r = (@l, 40)[1 .. 3];
say "@r"; #=> 20 30 40
sub okf { say $_[0] ? "ok" : "not ok" }
okf(open(my $fh, "<", "/dev/null")); #=> ok
say defined(<$fh>) ? "line" : "eof"; #=> eof
$ENV{FILEOPS_X} = "from env";
say $ENV{FILEOPS_X}; #=> from env
{ local $ENV{FILEOPS_X} = "local"; say $ENV{FILEOPS_X}; } #=> local
say $ENV{FILEOPS_X}; #=> from env
delete $ENV{FILEOPS_X};
say exists $ENV{FILEOPS_X} ? "set" : "unset"; #=> unset
use Config;
say $Config{osname} eq "linux" || $Config{osname} eq "MSWin32" || $Config{osname} eq "darwin" ? "osname" : "unknown osname"; #=> osname
open(PIPE, "echo two |") || die;
my @pl = <PIPE>;
close PIPE;
say scalar(@pl), " ", $pl[0] =~ /^two/ ? "two" : "other"; #=> 1 two
system("true");
say "status=$? ok"; #=> status=0 ok
