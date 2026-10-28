# next and last in a block called by grep (not in a loop of their own)
say (1..16).grep({last if $_ % 5 == 0; $_ % 2 == 0}).join(",");    #=> 2,4
say (1..12).grep({next if $_ % 5 == 0; $_ % 2 == 0}).join(",");    #=> 2,4,6,8,12
for 1, 2, 3 { next if $_ == 2; say $_ }                            #=> 1
                                                                   #=> 3
my $i = 0;
while $i < 4 { $i++; next if $i == 2; say $i }                     #=> 1
                                                                   #=> 3
                                                                   #=> 4
