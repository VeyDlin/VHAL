import './assets/main.css'

import { createApp } from 'vue'
import { createRouter, createWebHistory } from 'vue-router'
import ui from '@nuxt/ui/vue-plugin'
import App from './App.vue'

const router = createRouter({
  routes: [
    {
      path: '/',
      component: () => import('./pages/Home.vue')
    },
    {
      path: '/docs/:slug(.*)*',
      component: () => import('./pages/DocPage.vue')
    }
  ],
  history: createWebHistory('/VHAL/'),
  scrollBehavior(to) {
    if (to.hash) {
      return { el: to.hash, behavior: 'smooth' }
    }
    return { top: 0 }
  }
})

const app = createApp(App)

app.use(router)
app.use(ui)

app.mount('#app')
