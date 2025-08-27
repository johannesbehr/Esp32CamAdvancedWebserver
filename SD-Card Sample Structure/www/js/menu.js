window.addEventListener("error", (e) => {
  alert("JS-Fehler: " + e.message);
});

document.addEventListener("DOMContentLoaded", () => {
  const menuContainer = document.createElement("div");
  menuContainer.id = "hamburgerMenu";
  menuContainer.innerHTML = `
    <div id="menuButton">&#9776;</div>
    <div id="sideMenu" class="hidden">
      <div id="closeButton">&times;</div>
      <nav id="menuItems"></nav>
    </div>
  `;
  document.body.appendChild(menuContainer);

  fetch('/menu.json')
    .then(response => response.json())
    .then(links => {
      const menuItems = document.getElementById("menuItems");
      links.forEach(link => {
        const a = document.createElement("a");
        a.href = link.url;
        a.textContent = link.title;
        menuItems.appendChild(a);
      });
    });

  document.getElementById("menuButton").addEventListener("click", () => {
    document.getElementById("sideMenu").classList.remove("hidden");
  });

  document.getElementById("closeButton").addEventListener("click", () => {
    document.getElementById("sideMenu").classList.add("hidden");
  });
});
