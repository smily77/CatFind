void writelnComment(String comment) {
  if (DEBUG) Serial << comment  <<  endl;
  //tft << comment << endl;
  //gfx -> println(comment);
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
  //tft << comment;
  //gfx -> print(comment);
}
