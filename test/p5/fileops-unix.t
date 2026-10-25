#!perl
# p2-only: hard links, symlinks, permission bits and umask are unix only (skipped when WIN32=1, see test.p5 in the Makefile)
my $d = "fileops-unix-tmp.$$";
mkdir($d, 0755) || die;
open(F, ">$d/a") || die; print F "hello"; close F;
say link("$d/a", "$d/b") ? "link" : "no link"; #=> link
my @s = stat("$d/a");
say "$s[3] ", sprintf("%o", $s[2] & 0777) eq "644" || sprintf("%o", $s[2] & 0777) eq "664" ? "mode" : "other mode"; #=> 2 mode
say chmod(0600, "$d/a", "$d/b"); #=> 2
say sprintf("%o", (stat "$d/b")[2] & 0777); #=> 600
say symlink("a", "$d/l") ? readlink("$d/l") : "no symlink"; #=> a
opendir(D, $d) || die;
my @f = sort grep { $_ ne "." && $_ ne ".." } readdir(D);
closedir(D);
say "@f"; #=> a b l
say unlink("$d/a", "$d/b", "$d/l"); #=> 3
rmdir($d);
umask(022);
say umask(0); #=> 18
