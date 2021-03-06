extern proc memcpy(a:[], b, len);

// For consistency with the other shootouts, a value is passed to this
// benchmark in the Computer Language Benchmarks Game test suite, even though
// the input set provided is used to determine the problem size.  Store that
// passed value here.
config const n = 0;

proc string.toBytes() {
   var b : [1..this.length] uint(8);
   memcpy(b, this, this.length);
   return b;
}

const pairs = "ATCGGCTAUAMKRYWWSSYRKMVBHDDHBVNN\n\n".toBytes();
var table : [1..128] uint(8);

proc main() {
  param newLineChar = 0x0A;
  param greaterThan = 0x3E;
  var inFile = openfd(0); // stdin
  const fileLen = inFile.length();
  var data : [1..fileLen] uint(8);
  var r = inFile.reader(locking=false);

  // initialize complement table
  for i in 1..pairs.size by 2 {
    table[pairs[i]] = pairs[i+1];      // uppercase
    if pairs[i] != newLineChar then
      table[pairs[i] + 32] = pairs[i+1]; // lowercase
  }

  var numRead  : int;
  var idx = 1;
  var start = 0;

  // sync statements wait for all tasks inside them to complete
  sync {
    while r.readline(data, numRead, idx) {

      // Look for the start of a section, and if possible 
      // spawn a task to start work on the previous section.
      if data[idx] == greaterThan {
        if start == 0 then start = idx;
        else {
          begin process(data, start, idx-2);
          start = idx;
        }
      }
      idx += numRead; 
    }

    // work on the last section
    process(data, start, idx-2);
  }

  // Open a binary writer to stdout
  var binout = openfd(1).writer(iokind.native, locking=false, 
                                hints=QIO_CH_ALWAYS_UNBUFFERED);
  binout.write(data);
}

proc process(ref data : [], in from : int, end : int) {

  // Skip the header information
  while data[from] != 0xa do from += 1;
  from += 1;

  const len = end - from;
  const off = 60 - (len % 61);

  if off > 0 {
    var m = from + 60 - off;
    while m < end {
      for i in 0..off-1 by -1 do data[m+1+i] = data[m+i];
      data[m+1] = 0xa;
      m += 61;
    }
  }

  const middle = (end-from)/2;
  for i in 0 .. middle {
    const c = table[data[from+i]];
    data[from+i] = table[data[end-i]];
    data[end-i] = c;
  }
}
