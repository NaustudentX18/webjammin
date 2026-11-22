/* WeBjammin — landing page behaviour.
   Loads the pack track index and filters it live. No dependencies. */
(function () {
  "use strict";

  var list = document.getElementById("track-list");
  var empty = document.getElementById("track-empty");
  var search = document.getElementById("track-search");
  if (!list || !search) return;

  function el(tag, cls, text) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text !== undefined) n.textContent = text;
    return n;
  }

  function render(tracks) {
    list.textContent = "";
    var frag = document.createDocumentFragment();
    tracks.slice(0, 300).forEach(function (t) {
      var row = el("div", "track");
      row.appendChild(el("b", null, t.title));
      row.appendChild(el("span", null, t.artist));
      frag.appendChild(row);
    });
    list.appendChild(frag);
    empty.hidden = tracks.length > 0;
  }

  fetch("assets/gh3-tracks.json")
    .then(function (r) { return r.json(); })
    .then(function (data) {
      var all = data || [];
      render(all);
      search.addEventListener("input", function () {
        var q = search.value.trim().toLowerCase();
        if (!q) return render(all);
        var parts = q.split(/\s+/);
        var hits = all.filter(function (t) {
          var hay = (t.title + " " + t.artist).toLowerCase();
          return parts.every(function (p) { return hay.indexOf(p) !== -1; });
        });
        render(hits);
      });
      search.setAttribute("placeholder", "Search " + all.length + " tracks — title or artist…");
    })
    .catch(function () {
      list.textContent = "";
      empty.hidden = false;
      empty.textContent = "Track index unavailable (run site/tools/make_art.py to regenerate the assets).";
    });

  /* Smooth in-page navigation that respects the sticky header. */
  document.querySelectorAll('a[href^="#"]').forEach(function (a) {
    a.addEventListener("click", function (ev) {
      var id = a.getAttribute("href").slice(1);
      var target = document.getElementById(id);
      if (!target) return;
      ev.preventDefault();
      var top = target.getBoundingClientRect().top + window.pageYOffset - 74;
      window.scrollTo({ top: top, behavior: "smooth" });
      history.replaceState(null, "", "#" + id);
    });
  });
})();
