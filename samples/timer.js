var tick = function(i) {
  if (++i < 10) setTimeout(function() { tick(i); }, 500);
  print(i%2 ? "tick\n" : "tock\n");
};
tick(0);
