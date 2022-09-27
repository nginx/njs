/*---
includes: []
flags: []
negative:
  phase: runtime
---*/

String.toString = async () => {
    String.prototype.concat([String, {toString(){ throw String; }}]);
    throw 1;
};
String.valueOf = String;

(async function() { throw String; })()
