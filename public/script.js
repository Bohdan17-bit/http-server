form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const formData = Object.fromEntries(new FormData(form).entries()); // Перетворюємо в об’єкт
    const response = await fetch("/", {
        method: "POST",
        body: JSON.stringify(formData), // Перетворюємо об'єкт у JSON
        headers: {
            "Content-Type": "application/json" // Змінюємо Content-Type на JSON
        },
    });

    if (!response.ok) {
        console.error("HTTP помилка:", response.status, response.statusText);
        return;
    }

    try {
        const result = await response.json();
        console.log(result); // Має вивести: {status: "success", data: "Дані отримано"}
    } catch (error) {
        console.error("Помилка при розборі JSON:", error);
    }
});

